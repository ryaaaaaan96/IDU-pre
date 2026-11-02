#include "protocol.h"
#include "process_table.h"
#include "signal_keys.h"
#include "status/inc/status.h"
#include <modbus_support/rtu.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(forward_uart));
static uint8_t reply[256];
static size_t reply_size;
static K_SEM_DEFINE(response_ready, 0, 1);
static status_phase_t phase = STATUS_RUN;
static unsigned flash_writes;
static bool fail_flash_write;

static int write_byte(const struct device *device, off_t offset, uint8_t data)
{
    ARG_UNUSED(device);
    ARG_UNUSED(offset);
    ++flash_writes;
    return fail_flash_write ? -EIO : data;
}

static const struct flash_simulator_cb flash_callbacks = {
    .write_byte = write_byte,
};

status_phase_t status_phase_get(void)
{
    return phase;
}

static void transmitted(const struct device *device, size_t available, void *context)
{
    ARG_UNUSED(context);
    zassert_true(reply_size + available <= sizeof(reply));
    reply_size += uart_emul_get_tx_data(device, reply + reply_size, available);
    k_sem_give(&response_ready);
}

static void exchange_register(
    uint8_t function, uint16_t address, uint16_t value, struct modbus_adu *response)
{
    struct modbus_adu request = {.unit_id = 1, .fc = function, .length = 4};
    sys_put_be16(address, request.data);
    sys_put_be16(value, request.data + 2);
    uint8_t frame[256];
    size_t size;
    reply_size = 0;
    k_sem_reset(&response_ready);
    zassert_ok(mb_rtu_encode(&request, frame, sizeof(frame), &size));
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_ok(k_sem_take(&response_ready, K_SECONDS(1)));
    k_msleep(20);
    zassert_ok(mb_rtu_decode(reply, reply_size, response));
}

ZTEST(protocol_port, test_product_uart_thread_and_direction)
{
    uart_emul_callback_tx_data_ready_set(uart, transmitted, NULL);
    zassert_ok(protocol_init());
    if (getenv("IDU_PROTOCOL_VERIFY")) {
        struct modbus_adu response;
        const bool defaults = !strcmp(getenv("IDU_PROTOCOL_VERIFY"), "defaults");
        if (defaults) {
            exchange_register(3, 0, 1, &response);
            zassert_equal(response.fc, 3);
            zassert_equal(sys_get_be16(response.data + 1), CONFIG_IDU_SAMPLE_PERIOD_MS);
            exchange_register(3, 17, 1, &response);
            zassert_equal(response.fc, 3);
            zassert_equal(sys_get_be16(response.data + 1), CONFIG_IDU_SAMPLE_PERIOD_MS);
            exchange_register(3, 18, 1, &response);
            zassert_equal(response.fc, 3);
            zassert_equal(sys_get_be16(response.data + 1), 0);
            return;
        }
        exchange_register(3, 0, 1, &response);
        zassert_equal(response.fc, 3);
        zassert_equal(sys_get_be16(response.data + 1), 600);
        exchange_register(3, 17, 1, &response);
        zassert_equal(response.fc, 3);
        zassert_equal(sys_get_be16(response.data + 1), CONFIG_IDU_SAMPLE_PERIOD_MS);
        exchange_register(3, 16, 1, &response);
        zassert_equal(response.fc, 3);
        zassert_equal(sys_get_be16(response.data + 1), 0);

        /* 非法操作命令不执行动作；命令 2 恢复默认值后必须再保存。 */
        exchange_register(6, 18, 4, &response);
        zassert_equal(response.fc, 0x86);
        zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
        exchange_register(6, 18, 2, &response);
        zassert_equal(response.fc, 6);
        zassert_equal(sys_get_be16(response.data + 2), 2);
        exchange_register(3, 0, 1, &response);
        zassert_equal(response.fc, 3);
        zassert_equal(sys_get_be16(response.data + 1), CONFIG_IDU_SAMPLE_PERIOD_MS);
        exchange_register(3, 18, 1, &response);
        zassert_equal(response.fc, 3);
        zassert_equal(sys_get_be16(response.data + 1), 0);
        exchange_register(6, 16, 1, &response);
        zassert_equal(response.fc, 6);
        return;
    }
    const value_t sample = {.u16 = 0x8001};
    zassert_ok(dp_set_para_by_index(DEVICE_LOCAL, PROCESS_DI, VALUE_TYPE_U16, &sample));
    struct modbus_adu request = {.unit_id = 1, .fc = 4, .length = 4};
    sys_put_be16(0, request.data);
    sys_put_be16(1, request.data + 2);
    uint8_t frame[256];
    size_t size;
    zassert_ok(mb_rtu_encode(&request, frame, sizeof(frame), &size));
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_ok(k_sem_take(&response_ready, K_SECONDS(1)));
    k_msleep(20);
    struct modbus_adu response;
    zassert_ok(mb_rtu_decode(reply, reply_size, &response));
    zassert_equal(response.fc, 4);
    zassert_equal(sys_get_be16(response.data + 1), 0x8001);
    zassert_equal(gpio_emul_output_get(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 5), 0);
    reply_size = 0;
    k_sem_reset(&response_ready);
    request.fc = 6;
    sys_put_be16(0, request.data);
    sys_put_be16(600, request.data + 2);
    zassert_ok(mb_rtu_encode(&request, frame, sizeof(frame), &size));
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_ok(k_sem_take(&response_ready, K_SECONDS(1)));
    k_msleep(20);
    zassert_ok(mb_rtu_decode(reply, reply_size, &response));
    zassert_equal(response.fc, 6);
    value_t value;
    zassert_ok(dp_get_para_by_index(DEVICE_LOCAL, PROCESS_SAMPLE_PERIOD, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 600);

    /* 同一 DP 点经独立地址验证低字在前和放缩后的高字在前。 */
    for (uint16_t address = 2; address <= 4; address += 2) {
        reply_size = 0;
        k_sem_reset(&response_ready);
        request.fc = 3;
        sys_put_be16(address, request.data);
        sys_put_be16(2, request.data + 2);
        zassert_ok(mb_rtu_encode(&request, frame, sizeof(frame), &size));
        zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
        zassert_ok(k_sem_take(&response_ready, K_SECONDS(1)));
        k_msleep(20);
        zassert_ok(mb_rtu_decode(reply, reply_size, &response));
        zassert_equal(response.fc, 3);
        zassert_equal(response.data[0], 4);
        zassert_equal(sys_get_be16(response.data + 1), address == 2 ? 600 : 0);
        zassert_equal(sys_get_be16(response.data + 3), address == 2 ? 0 : 60000);
    }

    /* U32 行必须完整写入，单寄存器写入不能修改共享的采样周期。 */
    reply_size = 0;
    k_sem_reset(&response_ready);
    request.fc = 6;
    sys_put_be16(2, request.data);
    sys_put_be16(700, request.data + 2);
    zassert_ok(mb_rtu_encode(&request, frame, sizeof(frame), &size));
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_ok(k_sem_take(&response_ready, K_SECONDS(1)));
    k_msleep(20);
    zassert_ok(mb_rtu_decode(reply, reply_size, &response));
    zassert_equal(response.fc, 0x86);
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(dp_get_para_by_index(DEVICE_LOCAL, PROCESS_SAMPLE_PERIOD, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 600);
    /* 禁用本套件的短周期自动保存，确认是命令触发真实 Flash 写入。 */
    exchange_register(6, 17, 600, &response);
    zassert_equal(response.fc, 6);
    exchange_register(3, 17, 1, &response);
    zassert_equal(response.fc, 3);
    zassert_equal(sys_get_be16(response.data + 1), 600);
    flash_simulator_set_callbacks(
        DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller)), &flash_callbacks);
    fail_flash_write = true;
    exchange_register(6, 16, 1, &response);
    zassert_equal(response.fc, 0x86);
    zassert_equal(response.data[0], MODBUS_EXC_SERVER_DEVICE_FAILURE);
    zassert_true(flash_writes > 0);

    /* 写失败保留待保存值，重试成功才返回正常写响应。 */
    fail_flash_write = false;
    flash_writes = 0;
    exchange_register(6, 16, 1, &response);
    zassert_equal(response.fc, 6);
    zassert_equal(sys_get_be16(response.data), 16);
    zassert_equal(sys_get_be16(response.data + 2), 1);
    zassert_true(flash_writes > 0);
    exchange_register(3, 16, 1, &response);
    zassert_equal(response.fc, 3);
    zassert_equal(sys_get_be16(response.data + 1), 0);

    flash_writes = 0;
    exchange_register(6, 16, 1, &response);
    zassert_equal(response.fc, 6);
    zassert_equal(flash_writes, 0);
    for (uint16_t invalid = 0; invalid <= 2; invalid += 2) {
        exchange_register(6, 16, invalid, &response);
        zassert_equal(response.fc, 0x86);
        zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
        zassert_equal(flash_writes, 0);
    }
    flash_simulator_set_callbacks(
        DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller)), NULL);
    phase = STATUS_FAULT_LOCKED;
    reply_size = 0;
    k_sem_reset(&response_ready);
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_equal(k_sem_take(&response_ready, K_MSEC(100)), -EAGAIN);
    zassert_equal(reply_size, 0);
}

ZTEST_SUITE(protocol_port, NULL, NULL, NULL, NULL, NULL);
