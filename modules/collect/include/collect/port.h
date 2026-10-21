#ifndef CO_PORT_H
#define CO_PORT_H
#include <zephyr/modbus/modbus.h>

/* RAW 发送回调在打开端口时由采集适配器安装，默认不启用模拟设备。 */
#define CO_PORT_DEFAULT_RAW_TX NULL
#define CO_PORT_DEFAULT_RAW_USER NULL
#define CO_PORT_DEFAULT_SIMULATOR NULL
#define CO_PORT_DEFAULT_SIMULATION NULL

struct co_simulator;
struct co_simulator_config;
struct co_port_config {
    const char *iface_name;
    struct modbus_iface_param param;
    struct co_simulator *simulator;
    const struct co_simulator_config *simulation;
};

/* 在绑定传输层之后、co_init_bus 之前调用。成功返回 Zephyr 接口号。
 * 需要 CONFIG_CO_MODBUS_ADAPTER；关闭时返回 -ENOTSUP。
 */
int co_port_open(const struct co_port_config *config);
#endif
