/** @file @brief 工作队列任务监控、硬件喂狗和故障处理接口。 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

#define WATCHDOG_TASK_ACTIVE UINT32_C(1)
#define WATCHDOG_TASK_SLEEP_FOREVER UINT32_C(2)
#define WATCHDOG_TASK_FAULT UINT32_C(4)

struct watchdog_task_config {
    const char *name; /**< 常驻任务名称。 */
    uint32_t timeout_ms; /**< 允许的最大心跳间隔。 */
};

struct watchdog_task_snapshot {
    const char *name;
    uint32_t timeout_ms;
    int64_t last_feed_ms;
    uint32_t flags;
};

/** @brief 首次使用前清零；登记后保持常驻，内部字段由模块维护。 */
struct watchdog_task {
    struct watchdog_task_snapshot state;
    int64_t deadline_ms;
    struct watchdog_task *next;
};

/**
 * @brief 故障处理动作；上报方请求或 watchdog 内部默认值，回调只读。
 * @note 产品终止故障只使用 HOLD 或 RESET；可恢复异常由业务模块自行处理。
 */
typedef enum {
    WATCHDOG_ACTION_CONTINUE = 0, /**< 现有诊断兼容行为；不用于新的产品终止故障。 */
    WATCHDOG_ACTION_HOLD = 1, /**< 停止业务并保留现场，等待人工重启。 */
    WATCHDOG_ACTION_RESET = 2, /**< 停止业务并请求自动重启。 */
} watchdog_action_t;

typedef enum {
    WATCHDOG_FAULT_TASK, /**< 已登记任务超时。 */
    WATCHDOG_FAULT_CHECK, /**< 检查结果或首次检查宽限过期。 */
    WATCHDOG_FAULT_FEED_GPIO, /**< 外部看门狗喂狗 GPIO 初始化或电平翻转失败。 */
    WATCHDOG_FAULT_REPORTED, /**< 其他模块主动上报，附带描述和动作。 */
} watchdog_fault_reason_t;

/** @brief 统一故障快照；非任务故障的 task.name 为空。 */
struct watchdog_fault {
    watchdog_fault_reason_t reason;
    int error;
    int64_t occurred_ms;
    watchdog_action_t action; /**< 上报请求或 watchdog 默认动作，回调只读。 */
    uint32_t reset_delay_ms; /**< 仅 RESET 时有效，0 立即复位；回调只读。 */
    const char *description; /**< 常驻英文描述，主动上报由调用方保证字符串常驻。 */
    struct watchdog_task_snapshot task;
};

/** @brief 故障快照仅在回调期间有效，调用时不持有模块锁；回调只读，动作由上报方或 watchdog 决定。
 * @note 监控故障在系统 workqueue 通知；主动上报在调用者上下文同步通知，可能来自 ISR。
 * @note 回调须非阻塞、ISR 安全；不得递归调用 watchdog_report_fault。
*/
typedef int (*watchdog_fault_cb_t)(const struct watchdog_fault *snapshot);

typedef enum {
    WATCHDOG_STOP_MONITOR, /**< 关闭检查，保留硬件喂狗，用于应用显式留驻。 */
    WATCHDOG_STOP_ALL, /**< 同时停止检查和硬件喂狗。 */
} watchdog_stop_mode_t;

/**
 * @brief 显式启动模块，内部读取设备树；每次上电仅尝试一次，失败不重试。
 * @note 启动宽限和健康有效期均为检查周期的三倍；仅完整检查通过才续期。
 * @note 在线程中串行调用，notify 必须非空；初始化错误直接返回，不回调。
 */
int watchdog_init(watchdog_fault_cb_t notify);
/**
 * @brief 同步上报业务故障，通知初始化时绑定的唯一回调；线程和 ISR 均可调用。
 * @note 产品约定：仅上报本次启动不可恢复的故障，使用 HOLD 或 RESET；不提供清除或恢复业务接口。
 * @note 重试、离线重连等可恢复异常由业务模块处理，不通过此终止故障通道上报。
 * @note 通知前更新最近故障；不改写监控锁存；回调只读，动作由本次调用决定。
 * @note description 必须非空且常驻；故障阶段与处理策略由接收者判断。
 * @param reset_delay_ms 毫秒；仅动作为 RESET 时使用，其他动作忽略。0 立即复位。
 * @note 重复复位请求只允许提前期限；需要 CONFIG_REBOOT，延时使用相对超时，不依赖 CONFIG_TIMEOUT_64BIT。
 * @note 延时上限为 60 秒，更长的请求按 60 秒提前复位。
 * @retval 0 回调处理成功。
 * @retval -EINVAL 参数无效。
 * @retval -EACCES 尚未绑定回调。
 * @return 返回回调处理结果；立即复位通常不返回，延时复位安排后返回。
 */
int watchdog_report_fault(int error,
                          const char *description, watchdog_action_t action,
                          uint32_t reset_delay_ms);
/** @brief 非阻塞停止，可在 ISR 调用；不能重新启用任务监控。 */
void watchdog_stop(watchdog_stop_mode_t mode);
/** @brief 在线程中登记任务，配置值复制，名称与上下文保持常驻。 */
int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config);
/** @brief 在线程中报告进展；迟到返回 -ETIMEDOUT，并安排工作队列通知。 */
int watchdog_task_feed(struct watchdog_task *task);
/** @brief 在线程中退出期限监控，不使线程实际休眠；恢复须重新登记。 */
int watchdog_task_sleep_forever(struct watchdog_task *task);
/** @brief 复制任务快照，可从 ISR 调用，未登记返回 -ENOENT。 */
int watchdog_task_get(const struct watchdog_task *task, struct watchdog_task_snapshot *snapshot);
/**
 * @brief 复制最近主动上报或监控故障，可从 ISR 调用；没有故障返回 -ENOENT。
 * @note action/reset_delay_ms 对主动上报表示原始请求；查询不消费通知或改变监控状态。
 */
int watchdog_fault_get(struct watchdog_fault *snapshot);

#endif
