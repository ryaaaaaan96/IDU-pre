#ifndef MODBUS_SUPPORT_HOOKS_H
#define MODBUS_SUPPORT_HOOKS_H
#include "modbus_internal.h"
#include "transport_hooks.h"
/* 静默的启动检查；与 modbus_get_context() 不同，此处允许尚未配置。 */
int mb_support_check_available(int iface);
#ifdef CONFIG_MODBUS_SUPPORT_SERVER_HOOKS
int mb_support_submit_work(struct k_work *work);
bool mb_support_write_request(struct modbus_context *ctx, bool *reply);
bool mb_support_accept_request(struct modbus_context *ctx);
bool mb_support_suppress_broadcast(struct modbus_context *ctx);
void mb_support_complete_request(struct modbus_context *ctx, bool reply);
#else
static inline int mb_support_submit_work(struct k_work *work) { return k_work_submit(work); }
static inline bool mb_support_write_request(struct modbus_context *ctx, bool *reply)
{ return false; }
static inline bool mb_support_accept_request(struct modbus_context *ctx)
{ return !ctx->rx_adu.unit_id || ctx->rx_adu.unit_id == ctx->unit_id; }
static inline bool mb_support_suppress_broadcast(struct modbus_context *ctx)
{ return !ctx->rx_adu.unit_id; }
static inline void mb_support_complete_request(struct modbus_context *ctx, bool reply) {}
#endif
#endif
