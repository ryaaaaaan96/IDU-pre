#ifndef IDU_COMMUNICATION_PORTS_CONFIG_H
#define IDU_COMMUNICATION_PORTS_CONFIG_H

/* 业务端口号独立于 Zephyr iface 下标、点表 bus_id 和 UART 编号。 */
enum idu_communication_port {
    IDU_PORT_FORWARD = 1,
    IDU_PORT_ACQUISITION = 2,
    IDU_PORT_VIRTUAL_DEVICE = 3,
};

enum idu_communication_link {
    IDU_LINK_RS485 = 1,
    IDU_LINK_ACQUISITION = 2,
    IDU_LINK_VIRTUAL_DEVICE = 3,
};

int idu_communication_ports_init(void);

#endif
