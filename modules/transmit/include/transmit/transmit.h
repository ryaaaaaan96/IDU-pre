/**
 * @file
 * @brief 北向转发端口、过滤和状态接口。
 */
#ifndef TR_H
#define TR_H
#include <transmit/modbus_adapter.h>

/**
 * @brief 端口控制要求 CONFIG_TR_MODBUS_ADAPTER。
 */
typedef enum { TR_MODE_SLAVE, TR_MODE_SLEEP, TR_MODE_CALLBACK } tr_port_mode_t;
/**
 * @brief 各接口的模式和过滤处理函数接收已准入的完整 ADU。
 *
 * 返回 -ENOENT 继续、0 回复、TR_MODBUS_SILENT 静默接收， 或返回 -errno／正 Modbus 异常码。
 * CALLBACK 模式绝不回退到普通寄存器写入器。登记内容会复制， 用户数据须在使用期间有效。
 */
int tr_port_set_handler(int iface, tr_port_mode_t mode,
                       tr_modbus_route_fn handler, void *user);
int tr_port_get_mode(int iface, tr_port_mode_t *mode);
/**
 * @brief 只影响后续请求；进行中的处理函数或 TX 可以完成。
 *
 * SLEEP 禁止新的回调和回复，不中断串口发送。
 */
int tr_port_set_mode(int iface, tr_port_mode_t mode);
int tr_port_set_filter(int iface, tr_modbus_route_fn handler, void *user);
int tr_port_enable_filter(int iface, bool enabled);
struct tr_port_stats {
    uint32_t requests;       /**< 到达分发阶段的已准入请求数。 */
    uint32_t responses;      /**< 已准备的响应数，不代表物理 TX 完成数。 */
    uint32_t filtered;       /**< 被配置过滤器消费的请求数。 */
    uint32_t sleep_drops;    /**< 休眠期间丢弃的有效报文数。 */
    uint32_t callback_errors;
};
typedef enum { TR_PORT_GET_STATS, TR_PORT_CLEAR_STATS } tr_port_control_t;
/**
 * @brief GET 使用 struct tr_port_stats *，CLEAR 使用 NULL；要求启用适配实现。
 */
int tr_port_control(int iface, tr_port_control_t command, void *arg);
struct tr_connection_status {
    bool enabled, connected;
    int64_t last_activity;
};
int tr_port_connection_get(int iface, struct tr_connection_status *status);
struct tr_segment {
    uint16_t begin, end;
    uint8_t function; /**< 0 表示任意带寄存器地址 PDU 的功能码。 */
    tr_modbus_route_fn handler;
    void *user;
    uint16_t target_dev, target_begin; /**< handler 为 NULL 时使用。 */
};
struct tr_routes {
    const struct tr_segment *segments;
    size_t count;
};
/**
 * @brief 要求 CONFIG_TR_MODBUS_ADAPTER，否则返回 -ENOTSUP。
 *
 * 未启用 CONFIG_DP_PROCESS 或 CONFIG_COLLECT 时，直接读取和分发接口仍可使用。
 */
int tr_attach(int iface, const struct tr_routes *routes);
int tr_dispatch(int iface, const struct modbus_adu *request,
                          struct modbus_adu *response, void *user);
struct dp_equip_cfg;
/**
 * @brief 仅在启动期设置不可变视图。
 *
 * 发生任何显式绑定后， 未绑定端口都不再回退到旧版全局映射；校验失败保留原绑定。
 */
int tr_bind_mapping(uint16_t port, const struct dp_equip_cfg *view);
int tr_read(uint16_t port, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int tr_write(uint16_t port, uint16_t address, const uint16_t *words, size_t count);
int tr_read_register(uint8_t function, uint16_t address, uint16_t *value);
struct modbus_user_callbacks *tr_callbacks(void);
#endif
