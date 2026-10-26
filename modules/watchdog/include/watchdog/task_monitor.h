/** @file @brief 显式登记的软件任务看门狗；单例，不依赖硬件喂狗。 */
#ifndef WATCHDOG_TASK_MONITOR_H
#define WATCHDOG_TASK_MONITOR_H
#include <stdint.h>

/** @brief 超时通知在监控锁外调用，可能处于 ISR；只能执行非阻塞操作。 */
typedef void (*watchdog_task_timeout_cb_t)(void);
/** @brief 每次上电初始化一次；关闭软件监控时返回 -EACCES。 */
int watchdog_task_monitor_init(watchdog_task_timeout_cb_t notify);
/** @brief 永久停止任务监控，保留快照；复位前不能恢复。可在 ISR 调用。 */
void watchdog_task_monitor_stop(void);

/* 显式登记的任务监控接口。 */

/** @brief 任务处于活动期限监控状态。 */
#define WATCHDOG_TASK_ACTIVE UINT32_C(1)
/** @brief 任务已显式退出期限监控。 */
#define WATCHDOG_TASK_SLEEP_FOREVER UINT32_C(2)
/** @brief 任务已触发超时故障。 */
#define WATCHDOG_TASK_FAULT UINT32_C(4)

/** @brief 任务登记配置，字段值在登记时复制。 */
struct watchdog_task_config {
    const char *name; /**< 非空名称字符串，登记后须持续有效。 */
    uint32_t timeout_ms; /**< 最大心跳间隔，单位为毫秒，必须大于零。 */
};

/** @brief 任务监控状态的只读快照。 */
struct watchdog_task_snapshot {
    const char *name; /**< 调用方持有的任务名称。 */
    uint32_t timeout_ms; /**< 最大心跳间隔，单位为毫秒。 */
    int64_t last_feed_ms; /**< 最近登记或心跳时的系统运行时间，单位为毫秒。 */
    uint32_t flags; /**< 0 表示未登记，非零值为 WATCHDOG_TASK_* 状态标志。 */
};

/**
 * @brief 调用方持有的常驻任务上下文。
 * @note 首次使用前清零；登记后通过快照接口查询，不得修改内部字段。
 */
struct watchdog_task {
    struct watchdog_task_snapshot state; /**< 内部维护的诊断状态。 */
    int64_t deadline_ticks; /**< 内部维护的绝对到期时刻，单位为系统时钟节拍。 */
    struct watchdog_task *next; /**< 内部活动任务链表链接。 */
};

/** @brief 首个任务超时的快照。 */
struct watchdog_task_fault {
    struct watchdog_task_snapshot task; /**< 超时任务的诊断状态。 */
    int64_t occurred_ms; /**< 超时发生时的系统运行时间，单位为毫秒。 */
    int error; /**< 任务超时错误码。 */
};

/**
 * @brief 登记任务并立即开始期限监控，也可重新启用已退出监控的任务。
 * @param[in,out] task 首次使用前已清零的常驻任务上下文。
 * @param[in] config 任务名称和超时配置；名称须持续有效。
 * @retval 0 登记成功。
 * @retval -EINVAL 参数、名称或超时间隔无效。
 * @retval -EALREADY 任务已处于活动监控状态。
 * @retval -ECANCELED 监控已停止或任务超时。
 * @return 其他负错误码表示服务尚不可用。
 * @note 仅在线程中调用；失败保持上下文不变，不分配内存或占用固定槽位。
 */
int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config);

/**
 * @brief 报告活动任务进展并刷新期限。
 * @param[in,out] task 已登记且处于活动状态的任务上下文。
 * @retval 0 心跳已接收。
 * @retval -EACCES 任务未启用监控或服务不可用。
 * @retval -ETIMEDOUT 心跳到达时已经超时，并通知调用方。
 * @retval -ECANCELED 监控已停止或任务超时。
 * @return 其他负错误码表示参数、调用上下文。
 * @note 仅在线程中调用；不能通过迟到心跳撤销故障。
 */
int watchdog_task_feed(struct watchdog_task *task);

/**
 * @brief 取消任务的期限监控，保留休眠标志和诊断值。
 * @param[in,out] task 已登记的任务上下文。
 * @retval 0 已退出监控，或此前已经退出。
 * @retval -ETIMEDOUT 退出前已超时，并通知调用方。
 * @retval -ECANCELED 监控已停止或任务超时。
 * @return 其他负错误码表示参数、服务状态。
 * @note 仅在线程中调用，不使业务线程实际休眠；须重新登记才能恢复监控。
 */
int watchdog_task_sleep_forever(struct watchdog_task *task);

/**
 * @brief 读取任务快照，不修改状态，可在线程或 ISR 中调用。
 * @param[in] task 待查询的任务上下文。
 * @param[out] snapshot 接收快照的存储；失败时保持原值。
 * @retval 0 查询成功。
 * @retval -EINVAL 参数为空。
 * @retval -ENOENT 此任务从未登记。
 */
int watchdog_task_get(const struct watchdog_task *task, struct watchdog_task_snapshot *snapshot);

/**
 * @brief 读取首个任务超时快照，不修改状态，可在线程或 ISR 中调用。
 * @param[out] snapshot 接收快照的存储；失败时保持原值。
 * @retval 0 查询成功。
 * @retval -EINVAL 输出指针为空。
 * @retval -ENOENT 尚未发生任务超时。
 */
int watchdog_task_fault_get(struct watchdog_task_fault *snapshot);

#endif
