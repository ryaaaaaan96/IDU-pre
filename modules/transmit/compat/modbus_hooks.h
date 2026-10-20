#ifndef TR_MODBUS_HOOKS_H
#define TR_MODBUS_HOOKS_H
#include "modbus_internal.h"
#include "transport_hooks.h"
/* Quiet startup check; unlike modbus_get_context(), unconfigured is expected. */
int tr_modbus_check_available(int iface);
int tr_modbus_submit_work(struct k_work *work);
bool tr_modbus_write_request(struct modbus_context *ctx, bool *reply);
bool tr_modbus_accept_request(struct modbus_context *ctx);
bool tr_modbus_suppress_broadcast(struct modbus_context *ctx);
void tr_modbus_complete_request(struct modbus_context *ctx, bool reply);
#endif
