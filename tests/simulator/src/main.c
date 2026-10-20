#include <collect/port.h>
#include <collect/simulator.h>
#include <zephyr/ztest.h>

static const struct dp_point_cfg points[] = {
    {.key = {10, 1}, .source = DP_SOURCE_CONSTANT, .type = DP_U16,
     .width = 1, .maximum = 65535},
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 10, .unit_id = 3, .bus_id = 0},
    {.dev_id = 20, .unit_id = 9, .bus_id = 1},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
};
static const struct co_simulator_register registers[] = {
    {.function = 3, .address = 90, .width = 1, .unit_address = true},
    {.function = 3, .address = 100, .width = 1, .initial = 10,
     .writable = true, .minimum = 0, .maximum = 50},
    {.function = 3, .address = 101, .width = 2, .writable = true,
     .signed_value = true, .minimum = -1000, .maximum = 1000},
    {.function = 4, .address = 300, .width = 1, .initial = 42},
};
static const struct co_simulator_config configs[] = {
    {.client_iface = "RAW_0", .server_iface = "RAW_1", .dev_id = 10, .timeout_us = 50000,
     .registers = registers, .register_count = ARRAY_SIZE(registers)},
    {.client_iface = "RAW_2", .server_iface = "RAW_3", .dev_id = 20, .timeout_us = 50000,
     .registers = registers, .register_count = ARRAY_SIZE(registers)},
};
static struct co_simulator sims[2];
static int clients[2];

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    for (size_t i = 0; i < ARRAY_SIZE(sims); ++i) {
        struct co_port_config port = {.simulator = &sims[i], .simulation = &configs[i]};
        clients[i] = co_port_open(&port);
        zassert_true(clients[i] >= 0);
    }
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    for (size_t i = 0; i < ARRAY_SIZE(sims); ++i) {
        co_simulator_set_online(&sims[i], true);
        co_simulator_set_write_fault(&sims[i], CO_SIMULATOR_WRITE_OK);
        zassert_ok(co_simulator_set_register(&sims[i], 3, 100, 10));
        zassert_ok(co_simulator_set_register(&sims[i], 3, 101, 0));
        zassert_ok(co_simulator_set_register(&sims[i], 3, 102, 0));
    }
}

ZTEST(simulator, test_two_devices_have_independent_values_and_faults)
{
    uint16_t value;
    for (size_t i = 0; i < ARRAY_SIZE(sims); ++i) {
        zassert_ok(modbus_read_holding_regs(clients[i], devices[i].unit_id, 90, &value, 1));
        zassert_equal(value, devices[i].unit_id);
    }
    zassert_ok(modbus_write_holding_reg(clients[0], 3, 100, 33));
    zassert_ok(modbus_read_holding_regs(clients[1], 9, 100, &value, 1));
    zassert_equal(value, 10);
    co_simulator_set_online(&sims[0], false);
    zassert_equal(modbus_read_input_regs(clients[0], 3, 300, &value, 1), -ETIMEDOUT);
    zassert_ok(modbus_read_input_regs(clients[1], 9, 300, &value, 1));
    zassert_equal(value, 42);
}

ZTEST(simulator, test_multiword_write_is_atomic_and_rejects_split_values)
{
    uint16_t values[] = {22, 0xffff, 0xffff};
    zassert_ok(modbus_write_holding_regs(clients[0], 3, 100, values, 3));
    unsigned writes = co_simulator_write_count(&sims[0]);
    values[0] = 44; values[1] = 1001; values[2] = 0;
    zassert_not_equal(modbus_write_holding_regs(clients[0], 3, 100, values, 3), 0);
    zassert_not_equal(modbus_write_holding_reg(clients[0], 3, 102, 0), 0);
    zassert_equal(co_simulator_write_count(&sims[0]), writes);
    zassert_ok(modbus_read_holding_regs(clients[0], 3, 100, values, 3));
    zassert_equal(values[0], 22);
    zassert_equal(values[1], 0xffff);
    zassert_equal(values[2], 0xffff);
}

ZTEST(simulator, test_lost_ack_preserves_write_without_retry)
{
    co_simulator_set_write_fault(&sims[0], CO_SIMULATOR_WRITE_LOSE_ACK);
    unsigned writes = co_simulator_write_count(&sims[0]);
    zassert_equal(modbus_write_holding_reg(clients[0], 3, 100, 25), -ETIMEDOUT);
    zassert_equal(co_simulator_write_count(&sims[0]), writes + 1);
    co_simulator_set_write_fault(&sims[0], CO_SIMULATOR_WRITE_OK);
    uint16_t value;
    zassert_ok(modbus_read_holding_regs(clients[0], 3, 100, &value, 1));
    zassert_equal(value, 25);
}

ZTEST(simulator, test_invalid_register_layout_does_not_touch_running_device)
{
    struct co_simulator unused = {0};
    struct co_simulator_config config = configs[0];
    const struct co_simulator_register invalid[] = {
        {.function = 3, .address = 100, .width = 2},
        {.function = 3, .address = 101, .width = 1},
    };
    config.registers = invalid; config.register_count = ARRAY_SIZE(invalid);
    zassert_equal(co_simulator_start(&unused, &config), -EEXIST);
    zassert_equal(co_simulator_start(&sims[0], &configs[0]), clients[0]);
    zassert_equal(co_simulator_start(&sims[0], &configs[1]), -EALREADY);
    uint16_t value;
    zassert_ok(modbus_read_holding_regs(clients[0], 3, 100, &value, 1));
    zassert_equal(value, 10);
}

ZTEST_SUITE(simulator, NULL, setup, before, NULL, NULL);
