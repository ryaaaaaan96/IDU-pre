#ifndef TR_REGISTERS_H
#define TR_REGISTERS_H

#include <transmit/transmit.h>

struct tr_command {
    uint16_t address, expected_value, dev_id;
    int (*execute)(uint16_t dev_id);
};

enum tr_parameter_field {
    TR_PARAMETER_FLAGS, TR_PARAMETER_ERROR,
    TR_PARAMETER_SEQUENCE_LOW, TR_PARAMETER_SEQUENCE_HIGH,
};

struct tr_status_register {
    uint16_t address;
    enum tr_parameter_field field;
};

/* 配置由调用者持有，生命周期覆盖端口；不在模块内固定产品寄存器地址。 */
struct tr_register_service {
    const struct tr_command *commands;
    size_t command_count;
    const struct tr_status_register *status;
    size_t status_count;
    /* 可选完整 ADU 扩展（如 IAP），在标准寄存器长度校验之前执行。
     * -ENOENT 表示不处理，TR_MODBUS_SILENT 表示处理但不应答。
     * 扩展负责自己的长度、广播和权限校验；收到的请求已通过端口地址过滤。
     */
    int (*extension)(int iface, const struct modbus_adu *rx, struct modbus_adu *tx);
};

/* 完整请求入口：优先使用点表，其次状态寄存器/命令，普通写交给 writer。
 * 返回值沿用 tr_modbus_route_fn；状态寄存器不会覆盖已有产品点表。
 */
int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user);
/* Startup only; installs the router and its broadcast policy. */
int tr_register_attach(int iface, const void *config);
int tr_register_write(uint16_t address, const uint16_t *words, size_t count);

#endif
