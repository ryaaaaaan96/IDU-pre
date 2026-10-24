/**
 * @file
 * @brief 北向寄存器映射配置。
 */
#ifndef TR_REGISTERS_H
#define TR_REGISTERS_H

#include <transmit_lib.h>

#define TR_REGISTER_DEFAULT_ROUTES NULL
#define TR_REGISTER_DEFAULT_BROADCAST false

/**
 * @brief 配置由调用者持有，生命周期覆盖端口；不在模块内固定产品寄存器地址。
 *
 * 普通状态和命令归 DP 处理点表，此处只提供协议段和标准地址段。
 */
struct tr_register_service {
    const struct tr_routes *routes;
    /**
     * @brief 可选设备挂载的 PRO_ADDR_SLAVE / MB_TABLE_SEG_T 协议段表。
     *
     * 按请求首地址匹配，在标准 PDU 校验前处理；回调负责精确校验。 设备、段表和配置在 attach 前装配完成，端口运行期间不得修改。
     */
    const DEV_TABLE_S *device;
    /**
     * @brief 显式允许协议段收到广播；段外广播始终静默且不执行业务写。
     */
    bool route_broadcast;
    /**
     * @brief 转发声明直接持有映射所有权，与 device 互斥。
     */
    const MB_TABLE_SEG_T *segments;
    size_t segment_count;
    const struct dp_equip_cfg *mapping;

};

/**
 * @brief 完整请求入口：协议段、标准地址段、普通点表依次处理，普通写交给 writer。
 *
 * user 指向已通过 tr_register_attach() 校验的服务；端口先执行站号过滤。 返回值沿用 tr_modbus_route_fn。
 */
int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user);
/**
 * @brief 仅在启动期安装路由器及广播策略。
 */
int tr_register_attach(int iface, const void *config);
int tr_register_write(uint16_t address, const uint16_t *words, size_t count);

#endif
