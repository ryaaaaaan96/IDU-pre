#ifndef TR_MODBUS_PORT_H
#define TR_MODBUS_PORT_H
#include <transmit/message.h>
#include <zephyr/modbus/modbus.h>
/* 当前报文格式为完整 RTU 帧（地址、功能码、数据、CRC），虚拟口不解析它。
 * 启动期绑定 RAW 接口；io 常驻。无需启用物理串口即可回环或测试。
 */
int tr_modbus_port_bind(uint16_t port, const char *iface_name, const struct vport_io *io);
int tr_modbus_port_param(int iface, struct modbus_iface_param *param);
#endif
