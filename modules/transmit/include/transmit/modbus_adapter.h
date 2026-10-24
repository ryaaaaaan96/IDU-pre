/**
 * @file
 * @brief Modbus 服务端任务和请求路由接口。
 */
#ifndef TR_MODBUS_ADAPTER_H
#define TR_MODBUS_ADAPTER_H

#include <zephyr/modbus/modbus.h>
#include <errno.h>
#include <zephyr/kernel.h>

struct tr_modbus_execution {
    struct k_work_q *receive;
    struct k_work_q *configuration;
};
/**
 * @brief 仅在启动期复制绑定。
 *
 * 队列归应用持有，必须相互独立， 且在启用服务端前已启动。此处不创建线程。
 */
int tr_modbus_bind_execution(int iface, const struct tr_modbus_execution *execution);

/**
 * @brief 每个完整 FC06/FC16 PDU 仅调用一次，返回 0 或 -errno， 不逐字调用。
 *
 * 回调等待南向 Modbus 或 Flash 时必须 isolated=true； 非隔离测试回调不得阻塞等待协议事务。
 */
typedef int (*tr_modbus_write_fn)(uint16_t address, const uint16_t *words, size_t count);
/**
 * @brief 启动调用必须串行执行；已被占用的客户端或服务端返回 -EALREADY， 不修改写入器或队列。
 *
 * 启动失败会释放新预留的任务槽。 不得与直接 modbus_init_* 调用并发启动。
 */
int tr_modbus_init_server(int iface, struct modbus_iface_param param,
                           tr_modbus_write_fn write_fn, bool isolated);
/** @brief 路由接收但不回复时的返回值。 */
#define TR_MODBUS_SILENT (-ECANCELED)
/**
 * @brief 返回 -ENOENT 继续处理， TR_MODBUS_SILENT 表示接收但不回复。
 *
 * 仅接口显式启用广播策略时， 广播才会进入路由器。
 */
typedef int (*tr_modbus_route_fn)(int iface, const struct modbus_adu *request,
                                  struct modbus_adu *response, void *user);
int tr_modbus_set_router(int iface, tr_modbus_route_fn route, void *user);
struct tr_modbus_policy {
    /**
     * @brief NULL 表示保留标准的本站号和广播过滤。
     */
    bool (*accept)(const struct modbus_adu *request, uint8_t own_unit, void *user);
    bool route_broadcast; /**< 路由器负责广播处理和回复策略。 */
    void *user;
};
/**
 * @brief 仅启动期配置，内容会复制；NULL 恢复标准站号和广播过滤。
 */
int tr_modbus_set_policy(int iface, const struct tr_modbus_policy *policy);
int tr_modbus_request_iface(void);
uint8_t tr_modbus_request_function(void);
typedef void (*tr_modbus_config_done_fn)(int iface, int error, void *user);
/**
 * @brief 复制请求，在旧响应发送完成后应用。
 */
int tr_modbus_reconfigure(int iface, struct modbus_iface_param param,
                           tr_modbus_config_done_fn done, void *user);
/**
 * @brief 即使 TX 从未完成，也可复位当前后端。
 *
 * 必须先等待正在执行的回调结束， 不得在 Flash 写入中途取消处理函数。
 */
int tr_modbus_recover(int iface, tr_modbus_config_done_fn done, void *user);
/**
 * @brief 主循环健康维护：TX 空闲或阻塞超过 timeout_ms 后触发恢复； 处理函数卡住返回 -ETIMEDOUT，由板级看门狗复位 CPU。
 */
int tr_modbus_service(int iface, uint32_t timeout_ms);
struct tr_modbus_health { uint32_t recoveries; int last_error; bool changing; };
int tr_modbus_get_health(int iface, struct tr_modbus_health *health);

#endif
