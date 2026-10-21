#ifndef TR_MODBUS_HOOKS_H
#define TR_MODBUS_HOOKS_H
#include "modbus_internal.h"
#include "transport_hooks.h"
/* Quiet startup check; unlike modbus_get_context(), unconfigured is expected. */
int tr_modbus_check_available(int iface);
#ifdef CONFIG_TR_MODBUS_ADAPTER
int tr_modbus_submit_work(struct k_work *work);
bool tr_modbus_write_request(struct modbus_context *ctx, bool *reply);
bool tr_modbus_accept_request(struct modbus_context *ctx);
bool tr_modbus_suppress_broadcast(struct modbus_context *ctx);
void tr_modbus_complete_request(struct modbus_context *ctx, bool reply);
#else
static inline int tr_modbus_submit_work(struct k_work *work) { return k_work_submit(work); }
static inline bool tr_modbus_write_request(struct modbus_context *ctx, bool *reply)
{ return false; }
static inline bool tr_modbus_accept_request(struct modbus_context *ctx)
{ return !ctx->rx_adu.unit_id || ctx->rx_adu.unit_id == ctx->unit_id; }
static inline bool tr_modbus_suppress_broadcast(struct modbus_context *ctx)
{ return !ctx->rx_adu.unit_id; }
static inline void tr_modbus_complete_request(struct modbus_context *ctx, bool reply) {}
#endif
#endif
