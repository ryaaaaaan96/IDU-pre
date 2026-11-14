#ifndef MODBUS_SUPPORT_HOOKS_H
#define MODBUS_SUPPORT_HOOKS_H
#include "modbus_internal.h"
#include "transport_hooks.h"
/* 静默的启动检查；与 modbus_get_context() 不同，此处允许尚未配置。 */
int mb_support_check_available(int iface);
#ifdef CONFIG_MODBUS_SUPPORT_SERVER_HOOKS
int mb_support_submit_work(struct k_work *work);
int mb_support_raw_claim(struct modbus_context *ctx);
bool mb_support_accept_request(struct modbus_context *ctx);
uint8_t mb_support_exception_code(struct modbus_context *ctx, uint8_t exception);
#else
static inline int mb_support_submit_work(struct k_work *work)
{
    return modbus_work_submit(work);
}

static inline int mb_support_raw_claim(struct modbus_context *ctx)
{
    ARG_UNUSED(ctx);
    return 0;
}

static inline bool mb_support_accept_request(struct modbus_context *ctx)
{
    return !ctx->rx_adu.unit_id || ctx->rx_adu.unit_id == ctx->unit_id;
}

static inline uint8_t mb_support_exception_code(struct modbus_context *ctx, uint8_t exception)
{
    ARG_UNUSED(ctx);
    return exception;
}
#endif
#endif
