/**
 * @file
 * @brief Modbus 服务端的内部接入契约。
 */
#ifndef TR_SERVER_H
#define TR_SERVER_H

#include <transmit/modbus_adapter.h>

/**
 * @brief 缺少接口时服务保持禁用；有效站号和 RAW 发送回调由生命周期服务安装。
 */
#define TR_SERVER_DEFAULT_IFACE NULL
#define TR_SERVER_DEFAULT_USER_CALLBACK NULL
#define TR_SERVER_DEFAULT_UNIT 0U
#define TR_SERVER_DEFAULT_RAW_TX NULL
#define TR_SERVER_DEFAULT_RAW_USER NULL
#define TR_SERVER_DEFAULT_CONFIGURE NULL

struct tr_server_config {
    const char *iface_name;
    struct modbus_iface_param param;
    tr_modbus_write_fn write;
    int (*attach)(int iface, const void *protocol);
    const void *protocol;
    int (*configure)(struct modbus_iface_param *param);
    uint32_t recovery_timeout_ms;
};

/**
 * @brief 启动期显式初始化。
 *
 * 每个配置独占一个接口，模块保存活动/待生效状态。 配置必须为常驻对象；首次成功启动后不得修改。 configure
 * 可从业务参数生成最终串口设置；恢复期间下次调用重试。 不可从当前端口的接收/重配置回调调用本函数。
 */
int tr_server_init(const struct tr_server_config *config, uint8_t unit);
/**
 * @brief 应用任务周期维护；可显式更新 unit，初始化前返回 -ENODEV，不兼任首次启动。
 */
int tr_server_service(const struct tr_server_config *config, uint8_t unit);

#endif
