#include "protocol/ports.h"
#include "simulation.h"
#include "signal_ids.h"
#include <collect/port.h>
#include <collect/simulator.h>

/* 仅用于测试的模拟数据；收发、故障注入及整包写入校验由 collect 提供。 */
static const struct co_simulator_register registers[] = {
    {.function = 3, .address = 40000, .width = 1, .initial = 100,
     .writable = true, .minimum = 0, .maximum = 1000},
    {.function = 3, .address = 40001, .width = 2, .initial = 0,
     .writable = true, .signed_value = true, .minimum = -100000, .maximum = 100000},
    {.function = 3, .address = 30459, .width = 1, .unit_address = true},
    {.function = 4, .address = 8152, .width = 1, .initial = 0x0005},
    {.function = 4, .address = 8150, .width = 2, .initial = PROTOCOL_VERSION},
    {.function = 4, .address = 8163, .width = 1, .initial = 100},
    {.function = 4, .address = 8164, .width = 1, .initial = 200},
    {.function = 4, .address = 8165, .width = 1, .initial = 300},
    {.function = 4, .address = 8166, .width = 1, .initial = 400},
    {.function = 4, .address = 8167, .width = 1, .initial = 500},
    {.function = 4, .address = 8168, .width = 1, .initial = 600},
    {.function = 4, .address = 8169, .width = 1, .initial = 700},
    {.function = 4, .address = 8170, .width = 1, .initial = 800},
};
static const struct co_simulator_config simulation = {
    .client_iface = "RAW_0", .server_iface = "RAW_1", .dev_id = DEVICE_REMOTE_IO,
    .timeout_us = CONFIG_CO_TIMEOUT_MS * 1000U,
    .registers = registers, .register_count = ARRAY_SIZE(registers),
};
struct co_simulator idu_collect_simulator;
const struct co_port_config protocol_collect_port = {
    .simulator = &idu_collect_simulator, .simulation = &simulation,
};
