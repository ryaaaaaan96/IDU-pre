#ifndef MODBUS_SUPPORT_TRANSPORT_H
#define MODBUS_SUPPORT_TRANSPORT_H
#include <zephyr/modbus/modbus.h>
#include <zephyr/kernel.h>
/* Zephyr 4.4.2 构建期适配；不依赖 transmit 服务或业务表。 */
typedef int (*mb_serial_receive_fn)(void *user, const uint8_t *data, size_t size);
int mb_serial_open(const char *name, struct modbus_serial_param param,
                   mb_serial_receive_fn receive, void *user);
int mb_serial_send(int iface, const uint8_t *data, size_t size, k_timeout_t timeout);
int mb_serial_configure(int iface, struct modbus_serial_param param);
void mb_serial_release(int iface);
int mb_raw_set_done(int iface, void (*done)(void *user), void *user);
/* 有界同步客户端会话。begin 持有 Zephyr 可递归的接口锁；
 * 须在同一线程调用普通客户端接口，并始终调用 end。禁止嵌套；
 * 响应等待和 RAW 发送共用剩余总预算。
 * 未发送时的所有权等待超时返回 EBUSY，不算无响应。 */
int mb_client_begin(int iface, k_timeout_t timeout);
void mb_client_end(int iface);
k_timeout_t mb_client_remaining(int iface);
#endif
