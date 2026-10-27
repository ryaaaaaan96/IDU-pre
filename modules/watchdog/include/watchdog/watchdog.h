/**
 * @file
 * @brief 软件任务监控与外部 GPIO 看门狗统一接口。
 *
 * 外部喂狗实例仅清零一次，实例和常量配置须比定时器存活更久；软件任务监控为系统单例。
 *
 * 初始化须串行， 错误查询和停止可与定时器并发。 喂狗 GPIO 必须支持 ISR 安全翻转， 应使用片上 GPIO 而非可休眠总线。
 * GPIO 失败及显式停止在设备复位前不可恢复；停止后引脚保持最后电平， 让外部看门狗到期。
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/* ==================== 外部 GPIO 喂狗 ==================== */

/**
 * @brief 外部看门狗的常驻引脚和周期配置。
 */
struct watchdog_config {
    struct gpio_dt_spec feed;
    uint32_t feed_interval_ms;
};

/**
 * @brief 调用方持有的看门狗实例，字段由驱动维护。
 */
struct watchdog {
    struct k_timer timer;
    struct k_spinlock lock;
    const struct watchdog_config *config;
    int error;
    bool initialized;
    bool active;
};

/**
 * @brief 配置喂狗 GPIO 并启动周期边沿输出。
 * @param[in,out] monitor 首次使用前清零的常驻看门狗实例。
 * @param[in] config 常驻引脚和非零喂狗周期配置。
 * @retval 0 初始化成功。
 * @retval -EALREADY 此实例已初始化。
 * @return 其他负错误码表示配置无效或硬件初始化失败。
 */
int watchdog_init(struct watchdog *monitor,
                  const struct watchdog_config *config);
/**
 * @brief 读取底层喂狗错误，可与定时器并发调用。
 * @param[in] monitor 待查询的看门狗实例。
 * @retval 0 当前无错误。
 * @retval -EINVAL 实例指针为空。
 * @retval -EACCES 实例尚未初始化。
 * @return 其他负错误码表示 GPIO 错误或已经停止。
 */
int watchdog_error(struct watchdog *monitor);
/**
 * @brief 停止周期喂狗，使外部看门狗到期。
 * @param[in,out] monitor 待停止的看门狗实例；NULL 时直接返回。
 * @note 可与定时器并发调用；停止后保持最后输出电平，复位前不恢复喂狗。
 */
void watchdog_stop(struct watchdog *monitor);

/**
 * @brief 显式初始化设备树指定的外部看门狗，不向应用暴露引脚。
 * @note 在线程中串行调用，每次上电仅尝试一次；失败后不重试。
 * @retval 0 已开始周期喂狗，或外部功能关闭。
 * @retval -EALREADY 已尝试初始化或已停止。
 * @return 其他负错误码表示硬件初始化失败，查询接口保留错误。
 */
int watchdog_external_init(void);
/** @brief 查询外部看门狗；未初始化返回 -EACCES，关闭功能时返回 0。 */
int watchdog_external_error(void);
/** @brief 停止设备树实例的喂狗；关闭外部功能时无操作。 */
void watchdog_external_stop(void);

/* ==================== 软件任务监控 ==================== */

/**
 * @brief 超时通知在监控锁外调用，可能处于 ISR 或发现迟到心跳的调用线程。
 * @note 回调同步执行，必须短小、非阻塞；不得等待线程、操作 Flash 或同步输出日志。
 * @note 关键停喂/复位不应依赖可能阻塞的系统工作队列，允许延后的处理再提交工作。
 */
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
