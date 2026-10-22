#include "../../fixtures/point_connections.h"
#include "protocol/inc/protocol.h"
#include "protocol/inc/signal_ids.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <transmit/forward.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

BUILD_ASSERT(!IS_ENABLED(CONFIG_CO_SIMULATOR));
DEVICE_DT_DEFINE(DT_NODELABEL(modbus_test), NULL, NULL, NULL, NULL, POST_KERNEL, 90, NULL);
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart_test));
static const struct gpio_dt_spec de = GPIO_DT_SPEC_GET(DT_NODELABEL(modbus_test), de_gpios);
static uint8_t outgoing[CONFIG_MODBUS_BUFFER_SIZE], reply[CONFIG_MODBUS_BUFFER_SIZE];
static uint8_t slave_reply[CONFIG_MODBUS_BUFFER_SIZE];
static size_t outgoing_size, reply_size, slave_size;
static atomic_t requests, writes, fault;
static uint16_t setpoint = 100;
static uint16_t second_setpoint = 100;
static K_SEM_DEFINE(north_done, 0, 1);
static struct k_work_delayable respond;

enum { NORMAL, NO_REPLY, WRONG_UNIT, WRONG_FUNCTION, WRONG_LENGTH, BAD_CRC, EXCEPTION, NORTH_REQUEST };

/* 外部从机夹具通过 UART 注入完整帧，不链接产品内置模拟后端。 */
static void respond_work(struct k_work *work)
{
    ARG_UNUSED(work);
    zassert_equal(uart_emul_put_rx_data(uart, slave_reply, slave_size), slave_size);
}
static void transmitted(const struct device *dev, size_t bytes, void *user)
{
    ARG_UNUSED(bytes); ARG_UNUSED(user);
    outgoing_size += uart_emul_get_tx_data(dev, outgoing + outgoing_size,
                                           sizeof(outgoing) - outgoing_size);
    if (outgoing_size < 3) { return; }
    bool south = outgoing[0] == 1 || outgoing[0] == 2;
    size_t length = south ? (outgoing[1] == 16 ? (outgoing_size < 7 ? 0 : 9U + outgoing[6]) : 8) :
        ((outgoing[1] == 3 || outgoing[1] == 4) ? 5U + outgoing[2] : (outgoing[1] & 0x80) ? 5 : 8);
    if (!length || outgoing_size < length) { return; }
    zassert_equal(outgoing_size, length);
    zassert_equal(crc16_ansi(outgoing, length - 2), sys_get_le16(outgoing + length - 2));
    if (!south) {
        memcpy(reply, outgoing, length); reply_size = length; outgoing_size = 0;
        k_sem_give(&north_done); return;
    }
    atomic_inc(&requests);
    slave_reply[0] = outgoing[0]; slave_reply[1] = outgoing[1];
    uint16_t *target = outgoing[0] == 1 ? &setpoint : &second_setpoint;
    uint16_t address = sys_get_be16(outgoing + 2);
    if (outgoing[1] == 3 || outgoing[1] == 4) {
        uint16_t count = sys_get_be16(outgoing + 4);
        slave_size = 5 + 2 * count; slave_reply[2] = 2 * count;
        for (size_t i = 0; i < count; ++i) {
            uint16_t value = address == 40000 ? *target : outgoing[0] == 1 ? 0x55aa : 0x33cc;
            sys_put_be16(value, slave_reply + 3 + i * 2);
        }
    } else {
        *target = sys_get_be16(outgoing + (outgoing[1] == 6 ? 4 : 7));
        atomic_inc(&writes);
        memcpy(slave_reply + 2, outgoing + 2, 4); slave_size = 8;
    }
    outgoing_size = 0;
    switch (atomic_get(&fault)) {
    case NO_REPLY: return;
    case WRONG_UNIT: slave_reply[0] = 20; break;
    case WRONG_FUNCTION: slave_reply[1] = 7; break;
    case WRONG_LENGTH: slave_reply[2] += 2; break;
    case EXCEPTION: slave_reply[1] |= 0x80; slave_reply[2] = MODBUS_EXC_ILLEGAL_DATA_ADDR; slave_size = 5; break;
    case NORTH_REQUEST:
        slave_reply[0] = 16; slave_reply[1] = 3;
        sys_put_be16(31000, slave_reply + 2); sys_put_be16(1, slave_reply + 4);
        slave_size = 8; break;
    default: break;
    }
    sys_put_le16(crc16_ansi(slave_reply, slave_size - 2) ^ (atomic_get(&fault) == BAD_CRC),
                 slave_reply + slave_size - 2);
    k_work_reschedule(&respond, K_MSEC(3));
}
static void request(uint8_t fc, uint16_t address, uint16_t count_or_value)
{
    /* 模拟主站留出总线静默时间，然后发起一次完整请求。 */
    k_msleep(5); k_sem_reset(&north_done);
    uint8_t data[] = {16, fc, 0, 0, 0, 0, 0, 0};
    sys_put_be16(address, data + 2); sys_put_be16(count_or_value, data + 4);
    sys_put_le16(crc16_ansi(data, 6), data + 6);
    zassert_equal(uart_emul_put_rx_data(uart, data, sizeof(data)), sizeof(data));
    zassert_ok(k_sem_take(&north_done, K_MSEC(1500)));
    k_msleep(5);
    zassert_equal(reply[0], 16);
    zassert_equal(gpio_emul_output_get(de.port, de.pin), 0);
}
static void local_address(uint16_t address)
{
    zassert_ok(dp_set_sig_value(IDU_DEV_LOCAL, IDU_SIG_ADDRESS, sizeof(address), (uint8_t *)&address));
}
static void *setup(void)
{
    k_work_init_delayable(&respond, respond_work);
    uart_emul_callback_tx_data_ready_set(uart, transmitted, NULL);
    zassert_ok(idu_register_point_tables()); zassert_ok(dp_init());
    zassert_ok(idu_ports_init()); zassert_ok(idu_ports_init());
    zassert_true(co_port_open(&idu_collect_port) >= 0);
    fixture_connect_points();
    zassert_ok(co_init_bus(dp_config(), &idu_collect_bus));
    /* 本套件不测试跨进程恢复，每次从空存储开始，避免复用旧固件的 schema。 */
    const struct flash_area *area;
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)), &area));
    zassert_ok(flash_area_erase(area, 0, area->fa_size));
    flash_area_close(area);
    zassert_ok(dp_params_init(dp_config(), DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings))));
    /* Startup recovery can access the downstream before the first GPIO sample. */
    uint16_t value;
    zassert_ok(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1));
    zassert_ok(dp_process_init_device(0));
    local_address(16);
    zassert_ok(tr_forward_service(&idu_forwarding, 16));
    return NULL;
}
ZTEST(shared_rs485, test_collection_and_cached_north_read)
{
    zassert_ok(co_poll(0));
    struct dp_point_state state;
    for (int i = 0; i < 100; ++i) {
        zassert_ok(dp_read((struct dp_key){IDU_DEV_REMOTE_IO, IDU_SIG_DI}, &state));
        if (state.quality == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(state.quality, DP_QUALITY_GOOD);
    zassert_equal(state.value.words[0], 0x55aa);
    int before = atomic_get(&requests);
    request(4, 10000, 1);
    zassert_equal(reply[1], 4); zassert_equal(reply_size, 7);
    zassert_equal(sys_get_be16(reply + 3), 0x55aa);
    zassert_equal(atomic_get(&requests), before);
}
ZTEST(shared_rs485, test_two_devices_share_tables_but_keep_independent_values)
{
    zassert_equal(idu_device_count(), 3);
    zassert_equal(dp_config()->device_count, 2);
    for (size_t i = 0; i < dp_config()->collect_count; ++i) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(dp_config(), i, &scratch);
        if (poll->fields[0].sig_id == IDU_SIG_DI) { zassert_ok(co_poll(i)); }
    }
    struct dp_point_state first, second;
    for (int i = 0; i < 100; ++i) {
        zassert_ok(dp_read((struct dp_key){10, IDU_SIG_DI}, &first));
        zassert_ok(dp_read((struct dp_key){11, IDU_SIG_DI}, &second));
        if (first.quality == DP_QUALITY_GOOD && second.quality == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(first.quality, DP_QUALITY_GOOD);
    zassert_equal(second.quality, DP_QUALITY_GOOD);
    zassert_equal(first.value.words[0], 0x55aa);
    zassert_equal(second.value.words[0], 0x33cc);
}
ZTEST(shared_rs485, test_north_write_waits_for_south_reply_on_same_uart)
{
    int before = atomic_get(&writes);
    request(6, 31500, 321);
    zassert_equal(reply[1], 6); zassert_equal(reply_size, 8);
    zassert_equal(sys_get_be16(reply + 2), 31500);
    zassert_equal(sys_get_be16(reply + 4), 321);
    zassert_equal(setpoint, 321);
    zassert_equal(atomic_get(&writes), before + 1);
}
ZTEST(shared_rs485, test_timeouts_malformed_and_exception_responses_release_bus)
{
    uint16_t value;
    for (int f = NO_REPLY; f <= EXCEPTION; ++f) {
        atomic_set(&fault, f);
        int err = co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1);
        zassert_equal(err, f == EXCEPTION ? MODBUS_EXC_ILLEGAL_DATA_ADDR : -ETIMEDOUT, "fault=%d err=%d", f, err);
        atomic_set(&fault, NORMAL);
        zassert_ok(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1));
        zassert_equal(value, 0x55aa);
        request(3, 31000, 1); zassert_equal(reply[1], 3);
    }
}
ZTEST(shared_rs485, test_duplicate_local_and_remote_address_never_transmits)
{
    local_address(1);
    int before = atomic_get(&requests); uint16_t value;
    zassert_equal(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1), -EADDRINUSE);
    zassert_equal(atomic_get(&requests), before);
    local_address(16);
    zassert_ok(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1));
}
ZTEST(shared_rs485, test_north_request_during_collection_is_not_a_slave_response)
{
    uint16_t value;
    k_sem_reset(&north_done); atomic_set(&fault, NORTH_REQUEST);
    zassert_equal(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &value, 1), -ETIMEDOUT);
    zassert_equal(k_sem_count_get(&north_done), 0);
    atomic_set(&fault, NORMAL);
    request(3, 31000, 1); zassert_equal(reply[1], 3);
}
ZTEST(shared_rs485, test_z_periodic_worker_updates_cache_on_the_shared_wire)
{
    struct co_device_status before, after;
    struct co_device_status second_before, second_after;
    zassert_ok(co_device_status(IDU_DEV_REMOTE_IO, &before));
    zassert_ok(co_device_status(IDU_DEV_REMOTE_IO + 1, &second_before));
    zassert_ok(co_start());
    k_msleep(1200);
    /* Wait for the active poll to finish before disabling periodic traffic. */
    zassert_ok(co_set_port_mode(0, MODE_TRAN, 500));
    zassert_ok(co_device_status(IDU_DEV_REMOTE_IO, &after));
    zassert_ok(co_device_status(IDU_DEV_REMOTE_IO + 1, &second_after));
    /* 每台三个周期测点，两个设备都应完成多于一轮；不能用全局采集项数比较单台统计。 */
    zassert_true(after.successes > before.successes + 3);
    zassert_true(second_after.successes > second_before.successes + 3);
    int count = atomic_get(&requests);
    request(4, 10000, 1); zassert_equal(reply[1], 4);
    zassert_equal(atomic_get(&requests), count);
}
ZTEST_SUITE(shared_rs485, NULL, setup, NULL, NULL, NULL);
