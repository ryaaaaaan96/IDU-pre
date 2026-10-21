#ifndef CO_MODBUS_PORT_H
#define CO_MODBUS_PORT_H
#include <collect/message.h>
#include <zephyr/modbus/modbus.h>
/* 当前报文格式为完整 RTU 帧（地址、功能码、数据、CRC），虚拟口不解析它。
 * 启动期绑定 RAW 接口；io 常驻。无需启用物理串口即可回环或测试。
 */
int co_modbus_port_bind(uint16_t port, const char *iface_name, const struct vport_io *io);
int co_modbus_port_param(int iface, struct modbus_iface_param *param);
/* 同步协议事务；可由 co_bus_cfg.transfer 包装以编排共享物理链路。
 * 调用线程必须允许阻塞；RAW 内置路径支持 FC03/04/06/16。
 */
int co_modbus_transfer(int iface, struct modbus_adu *adu);
#endif
