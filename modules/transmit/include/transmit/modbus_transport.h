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
 * 对应 vport 路由的类型为 VPORT_DATAGRAM。
 */
int tr_modbus_transport_bind(const struct tr_modbus_transport *endpoints, size_t count);

/* 应用在 vport 收发回调中选择 endpoint，再调用这些接口。
 * send 仅用于正在执行的 Zephyr Modbus 发送交接，不创建新的协议事务；
 * endpoint 必须是已注册数组中的原对象，port 必须与该绑定一致。
 * RTU 经 Zephyr 串口驱动发送并等待完成；RAW 调用已注册的仿真/网络接口。
 * receive 将报文交还对应协议事务，丢弃来源不符或已过期的交接。
 */
int tr_modbus_transport_send(const struct tr_modbus_transport *endpoint, uint16_t port,
                             uint64_t peer, const struct vport_frame *frame,
                             k_timeout_t timeout);
void tr_modbus_transport_receive(const struct tr_modbus_transport *endpoint,
                                 const struct vport_source *source,
                                 const struct vport_frame *frame);

#endif
