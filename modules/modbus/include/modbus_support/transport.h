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
/* A bounded synchronous client session. begin owns Zephyr's recursive iface
 * lock; call its ordinary client API on the same thread, then always end.
 * No nesting. Response wait and RAW send use the remaining total budget.
 * Waiting for ownership without sending returns EBUSY, not a missing reply. */
int mb_client_begin(int iface, k_timeout_t timeout);
void mb_client_end(int iface);
k_timeout_t mb_client_remaining(int iface);
#endif
