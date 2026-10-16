#ifndef IDU_MODBUS_HOOKS_H
#define IDU_MODBUS_HOOKS_H
#include "modbus_internal.h"
int idu_modbus_submit_work(struct k_work *work);
bool idu_modbus_write_request(struct modbus_context *ctx, bool *reply);
bool idu_modbus_accept_request(struct modbus_context *ctx);
bool idu_modbus_suppress_broadcast(struct modbus_context *ctx);
#endif
