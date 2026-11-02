#include "protocol.h"
#include "process_table.h"
#include "signal_keys.h"
#include "status/inc/status.h"
#include <modbus_support/rtu.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(forward_uart));
static uint8_t reply[256];
static size_t reply_size;
static K_SEM_DEFINE(response_ready, 0, 1);
static status_phase_t phase = STATUS_RUN;

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

ZTEST(protocol_port, test_product_uart_thread_and_direction)
{
    uart_emul_callback_tx_data_ready_set(uart, transmitted, NULL);
    zassert_ok(protocol_init());
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
    phase = STATUS_FAULT_LOCKED;
    reply_size = 0;
    k_sem_reset(&response_ready);
    zassert_equal(uart_emul_put_rx_data(uart, frame, size), size);
    zassert_equal(k_sem_take(&response_ready, K_MSEC(100)), -EAGAIN);
    zassert_equal(reply_size, 0);
}

ZTEST_SUITE(protocol_port, NULL, NULL, NULL, NULL, NULL);
