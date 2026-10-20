#ifndef TR_MODBUS_TRANSPORT_H
#define TR_MODBUS_TRANSPORT_H

#include <vport/vport.h>
#include <zephyr/modbus/modbus.h>

/* 内部报文：4 字节交接代次 + 8 字节 MBAP 头 + ADU data。
 * 不是 UART 线格式；RTU 的 CRC/帧间隔仍由 Zephyr 管理。
 */
#define TR_MODBUS_TRANSPORT_MTU (CONFIG_MODBUS_BUFFER_SIZE + 8)

struct tr_modbus_transport {
    const char *iface_name;
    uint16_t port;
    uint16_t link;
};

/* 启动期绑定，先于 vport_init 和 modbus_init_*；配置持续有效至重启。
 * 一条 Zephyr 接口只绑定一个物理链路及协议端口，不复用为两个 RTU 角色。
 * vport 的 link.context / port.user 指向此配置，类型为 VPORT_DATAGRAM。
 */
int tr_modbus_transport_bind(const struct tr_modbus_transport *endpoints, size_t count);
extern const struct vport_backend_ops tr_modbus_transport_backend;
void tr_modbus_transport_receive(const struct vport_source *source,
                          const struct vport_frame *frame, void *user);

struct tr_modbus_network {
    const struct tr_modbus_transport *endpoints;
    size_t endpoint_count;
    const struct vport_config *ports;
};
/* 启动期组合注册；同一个常驻配置可重复调用，不接受运行期更换配置。 */
int tr_modbus_network_start(const struct tr_modbus_network *config);

#endif
