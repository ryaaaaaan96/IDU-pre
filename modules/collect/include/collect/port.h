#ifndef CO_PORT_H
#define CO_PORT_H
#include <zephyr/modbus/modbus.h>

struct co_simulator;
struct co_simulator_config;
struct co_port_config {
    const char *iface_name;
    struct modbus_iface_param param;
    struct co_simulator *simulator;
    const struct co_simulator_config *simulation;
};

/* 在绑定传输层之后、co_init_bus 之前调用。成功返回 Zephyr 接口号。 */
int co_port_open(const struct co_port_config *config);
#endif
