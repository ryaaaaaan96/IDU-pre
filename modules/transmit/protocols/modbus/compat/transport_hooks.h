#ifndef TR_MODBUS_TRANSPORT_HOOKS_H
#define TR_MODBUS_TRANSPORT_HOOKS_H

struct modbus_context;
#ifdef CONFIG_TR_MODBUS_TRANSPORT
int tr_modbus_transport_rx_begin(struct modbus_context *ctx);
void tr_modbus_transport_rx_end(struct modbus_context *ctx);
int tr_modbus_transport_rx_route(struct modbus_context *ctx);
bool tr_modbus_transport_tx_route(struct modbus_context *ctx, int *error);
void tr_modbus_transport_tx_done(struct modbus_context *ctx);
/* 由构建目录中的 serial 适配实现，仅支持中断 UART。 */
void tr_modbus_transport_serial_abort(struct modbus_context *ctx);
#else
static inline int tr_modbus_transport_rx_begin(struct modbus_context *ctx) { return 0; }
static inline void tr_modbus_transport_rx_end(struct modbus_context *ctx) {}
static inline int tr_modbus_transport_rx_route(struct modbus_context *ctx) { return 0; }
static inline bool tr_modbus_transport_tx_route(struct modbus_context *ctx, int *error)
{
    return false;
}
static inline void tr_modbus_transport_tx_done(struct modbus_context *ctx) {}
#endif
#endif
