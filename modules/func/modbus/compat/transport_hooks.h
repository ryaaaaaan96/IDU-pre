#ifndef MODBUS_IO_HOOKS_H
#define MODBUS_IO_HOOKS_H
#include <modbus_support/transport.h>
struct modbus_context;
int mb_raw_rx_begin(struct modbus_context *ctx);
void mb_raw_rx_end(struct modbus_context *ctx);
bool mb_serial_dispatch(struct modbus_context *ctx);
void mb_serial_tx_done(struct modbus_context *ctx);
void mb_serial_overflow(struct modbus_context *ctx);
void mb_serial_abort(struct modbus_context *ctx);
#endif
