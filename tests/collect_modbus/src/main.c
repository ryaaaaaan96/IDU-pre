#include <collect/collect.h>
#include <collect/port.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <errno.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_CO_MODBUS_ADAPTER));
BUILD_ASSERT(!IS_ENABLED(CONFIG_TRANSMIT));
BUILD_ASSERT(!IS_ENABLED(CONFIG_MODBUS_SERVER));

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX},
};
static const struct dp_device_cfg devices[] = {{.dev_id = 1, .unit_id = 7}};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
};
static uint16_t registers[4];
static bool lose_ack;
static unsigned writes;

/* 仅模拟远端响应；本机使用真实 Zephyr 客户端，不编译服务端或转发适配。 */
static int respond(int iface, const struct modbus_adu *request, void *user)
{
    ARG_UNUSED(user);
    zassert_equal(request->unit_id, 7);
    uint16_t address = sys_get_be16(request->data);
    uint16_t count = request->fc == 6 ? 1 : sys_get_be16(request->data + 2);
    zassert_true(count && address >= 100 && address + count <= 104);
    struct modbus_adu response = *request;

    if (request->fc == 3 || request->fc == 4) {
        response.data[0] = count * 2;
        response.length = 1 + count * 2;
        for (size_t i = 0; i < count; ++i) {
            sys_put_be16(registers[address - 100 + i], response.data + 1 + 2 * i);
        }
    } else {
        zassert_true(request->fc == 6 || request->fc == 16);
        for (size_t i = 0; i < count; ++i) {
            registers[address - 100 + i] = sys_get_be16(request->data +
                (request->fc == 6 ? 2 : 5) + 2 * i);
        }
        ++writes;
        response.length = 4;
        if (lose_ack) { return 0; }
    }
    return modbus_raw_submit_rx(iface, &response);
}

static const struct co_port_config port = {
    .iface_name = "RAW_0",
    .param = {.mode = MODBUS_MODE_RAW, .rx_timeout = 20000,
              .rawcb = {.raw_tx_cb = respond}},
};

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    int iface = co_port_open(&port);
    zassert_true(iface >= 0);
    zassert_ok(co_init(&equipment, iface, false));
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    for (size_t i = 0; i < ARRAY_SIZE(registers); ++i) { registers[i] = 10 + i; }
    lose_ack = false;
    writes = 0;
}

ZTEST(collect_modbus, test_read_write_and_reconfigure_without_transmit)
{
    uint16_t readback[4];
    zassert_ok(co_read(1, 4, 100, readback, ARRAY_SIZE(readback)));
    zassert_mem_equal(readback, registers, sizeof(registers));
    const uint16_t single = 123;
    const uint16_t multiple[] = {45, 67, 89};
    zassert_ok(co_write(1, 100, &single, 1));
    zassert_ok(co_write(1, 101, multiple, ARRAY_SIZE(multiple)));
    zassert_equal(writes, 2);
    zassert_ok(co_reconfigure(0, port.param));
    zassert_ok(co_read(1, 3, 100, readback, ARRAY_SIZE(readback)));
    zassert_equal(readback[0], single);
    zassert_mem_equal(readback + 1, multiple, sizeof(multiple));
}

ZTEST(collect_modbus, test_lost_write_ack_is_not_retried)
{
    uint16_t value = 456;
    lose_ack = true;
    zassert_equal(co_write(1, 100, &value, 1), -ETIMEDOUT);
    zassert_equal(writes, 1);
    struct co_device_status status;
    zassert_ok(co_device_status(1, &status));
    zassert_equal(status.error, -ETIMEDOUT);
    lose_ack = false;
    zassert_ok(co_read(1, 3, 100, &value, 1));
    zassert_equal(value, 456);
    zassert_equal(writes, 1);
}

ZTEST_SUITE(collect_modbus, NULL, setup, before, NULL, NULL);
