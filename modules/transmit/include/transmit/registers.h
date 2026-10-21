#ifndef TR_REGISTERS_H
#define TR_REGISTERS_H

#include <transmit/transmit.h>

/* 配置由调用者持有，生命周期覆盖端口；不在模块内固定产品寄存器地址。
 * 普通状态和命令归 DP 处理点表，此处只提供地址段和完整报文扩展。
 */
struct tr_register_service {
    const struct tr_routes *routes;
    /* 可选完整 ADU 扩展（如 IAP），在标准寄存器长度校验之前执行。
     * -ENOENT 表示不处理，TR_MODBUS_SILENT 表示处理但不应答。
     * 扩展负责自己的长度、广播和权限校验；收到的请求已通过端口地址过滤。
     */
    int (*extension)(int iface, const struct modbus_adu *rx, struct modbus_adu *tx);
};

/* 完整请求入口：扩展、地址段、普通点表依次处理，普通写交给 writer。
 * 返回值沿用 tr_modbus_route_fn。
 */
int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user);
/* Startup only; installs the router and its broadcast policy. */
int tr_register_attach(int iface, const void *config);
int tr_register_write(uint16_t address, const uint16_t *words, size_t count);

#endif
