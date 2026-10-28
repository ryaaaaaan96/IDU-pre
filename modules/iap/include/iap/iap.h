/**
 * @file
 * @brief 固件升级接收、路由和提交接口。
 */
#ifndef IAP_H
#define IAP_H

#include <iap/sgz.h>
#include <errno.h>
#include <stdbool.h>

struct modbus_adu;

struct iap_config {
    uint8_t staging_area_id;
    uint8_t journal_area_id;
    const uint8_t *aes_key; /**< 16 字节旧版传输互操作密钥。 */
    struct iap_image_policy image;
    /**
     * @brief 可选的配对钩子，在验签后的 IAP 线程中调用。
     *
     * prepare 须持久化应用状态并冻结修改；失败时自行撤销部分操作。 仅在 prepare
     * 成功而日志或升级提交失败时调用 cancel。
     */
    int (*prepare_reboot)(void *user);
    void (*cancel_reboot)(void *user);
    void *user;
};

/**
 * @brief 单接收器，在应用启动、安装路由前初始化一次。
 *
 * 配置会复制， 密钥和用户上下文须在其整个生命周期内有效。 初始化失败会释放 Flash 句柄，允许重试。
 * 使用 MCUboot slot0/slot1， 暂存与 NVS 的擦除单元为 4 KiB。重启后的健康确认仍归应用负责。
 */
int iap_init_config(const struct iap_config *config);
bool iap_ready(void);
/**
 * @brief 可在 ISR 中调用，终止状态保持到复位。
 *
 * 拒绝新路由和作业， 当前 Flash 操作安全完成；已准入的升级提交不回滚。
 */
void iap_stop(void);
/**
 * @brief CONFIG_IAP_CONFIGURED：使用相同的显式初始化，
 * 配置来自设备树、生成的验签及传输密钥，以及所选复位策略。
 */
int iap_init(void);

/** @brief 路由接收但不回复时的返回值。 */
#define IAP_ROUTE_SILENT (-ECANCELED)
/**
 * @brief 调用方提供已按站号过滤的完整 Modbus ADU， 包括旧版超长 FC16 报文。
 *
 * 返回 0 表示回复， -ENOENT 表示地址不归本模块， IAP_ROUTE_SILENT
 * 表示接收但不回复。此处不登记传输层。 接口 ID 必须非负，多个端口共用一个上传所有者。 登记到
 * PRO_ADDR_SLAVE 表时，TR 按起始地址选择路由， 将 -ENOENT 作为地址错误，不回退到测点写入。
 */
int iap_route(int iface, const struct modbus_adu *request, struct modbus_adu *response);

#endif
