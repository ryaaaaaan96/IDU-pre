/** @file @brief 工作队列任务监控、硬件喂狗和统一复位接口。 */
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

typedef enum {
    WATCHDOG_FAULT_TASK, /**< 已登记任务超时。 */
    WATCHDOG_FAULT_CHECK, /**< 检查结果或首次检查宽限过期。 */
    WATCHDOG_FAULT_GPIO, /**< 硬件引脚初始化或翻转失败。 */
} watchdog_fault_reason_t;

/** @brief 模块锁存的故障；非任务故障的 task.name 为空。 */
struct watchdog_fault {
    watchdog_fault_reason_t reason;
    int error;
    int64_t occurred_ms;
    struct watchdog_task_snapshot task;
};

/** @brief 故障通知只在系统 workqueue 中调用；snapshot 仅在回调期间有效。 */
typedef void (*watchdog_fault_cb_t)(const struct watchdog_fault *snapshot);

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
/** @brief 非阻塞停止，可在 ISR 调用；不能重新启用任务监控。 */
void watchdog_stop(watchdog_stop_mode_t mode);
/** @brief 同步停喂并调用底层冷复位；不等待工作队列或日志。需 CONFIG_REBOOT。 */
void watchdog_reset(void);

/** @brief 在线程中登记任务，配置值复制，名称与上下文保持常驻。 */
int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config);
/** @brief 在线程中报告进展；迟到返回 -ETIMEDOUT，并安排工作队列通知。 */
int watchdog_task_feed(struct watchdog_task *task);
/** @brief 在线程中退出期限监控，不使线程实际休眠；恢复须重新登记。 */
int watchdog_task_sleep_forever(struct watchdog_task *task);
/** @brief 复制任务快照，可从 ISR 调用，未登记返回 -ENOENT。 */
int watchdog_task_get(const struct watchdog_task *task, struct watchdog_task_snapshot *snapshot);
/** @brief 复制锁存故障，可从 ISR 调用；没有故障返回 -ENOENT，不改变状态。 */
int watchdog_fault_get(struct watchdog_fault *snapshot);

#endif
