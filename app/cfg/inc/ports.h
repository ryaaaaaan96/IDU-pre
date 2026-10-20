#ifndef IDU_CFG_PORTS_H
#define IDU_CFG_PORTS_H

/* 产品只提供模块配置，端口生命周期/协议解析由各模块实现。 */
struct tr_modbus_network;
struct tr_server_config;
struct co_port_config;
struct co_bus_cfg;
struct co_simulator;
struct tr_register_service;
struct tr_mvio_config;

extern const struct tr_modbus_network idu_port_network;
extern const struct co_port_config idu_collect_port;
extern const struct tr_server_config idu_forwarding;
extern const struct tr_register_service idu_parameter_registers;
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
extern struct co_simulator idu_collect_simulator;
#endif
#if defined(CONFIG_IDU_AIDC_PROFILE)
extern const struct co_bus_cfg idu_model_bus;
#else
extern const struct tr_mvio_config idu_mvio_protocol;
#endif

enum idu_communication_port {
    IDU_PORT_FORWARD = 1, IDU_PORT_ACQUISITION = 2, IDU_PORT_VIRTUAL_DEVICE = 3,
};
enum idu_communication_link {
    IDU_LINK_RS485 = 1, IDU_LINK_ACQUISITION = 2, IDU_LINK_VIRTUAL_DEVICE = 3,
};
#endif
