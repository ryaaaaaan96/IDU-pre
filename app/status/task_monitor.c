#include "status/internal.h"
#include <errno.h>

static struct {
    struct status_task *active;
    struct status_fault_snapshot fault;
} tasks;
static void task_timer_expired(struct k_timer *timer);
static K_TIMER_DEFINE(task_timer, task_timer_expired, NULL);

/* 调用方持有状态锁；这里只停止计时，不修改登记及诊断记录。 */
void status_tasks_stop(void)
{
    k_timer_stop(&task_timer);
}

static void task_expired(void)
{
    status_fault_phase_t phase = STATUS_RUNTIME;
    if (status_phase_get() == STATUS_STARTING) { phase = STATUS_INIT; }
    (void)status_report_fault(phase, -ETIMEDOUT, "Task heartbeat timed out",
                              phase == STATUS_INIT ? STATUS_ACTION_HOLD : STATUS_ACTION_RESET);
}

/* 调用方持有锁。
 * 任务快照也用于封闭“已确定超时、尚未在锁外执行故障策略”的短暂窗口。 */
static bool task_monitor_stopped(void)
{
    return status_runtime.phase == STATUS_FAULT_LOCKED || tasks.fault.task.name;
}

/* 调用方持有状态锁。 */
static struct status_task *earliest_task(void)
{
    struct status_task *first = NULL;
    for (struct status_task *task = tasks.active; task; task = task->next) {
        if (!first || task->deadline_ticks < first->deadline_ticks) {
            first = task;
        }
    }
    return first;
}

/* 用一个单次定时器跟踪最早期限。 */
static void schedule_next_task(void)
{
    struct status_task *next = earliest_task();
    if (task_monitor_stopped() || !next) {
        k_timer_stop(&task_timer);
    } else {
        k_timer_start(&task_timer, K_TIMEOUT_ABS_TICKS(next->deadline_ticks), K_NO_WAIT);
    }
}

static int task_service_error(void)
{
    if (task_monitor_stopped()) { return -ECANCELED; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (status_runtime.phase == STATUS_UNCONFIGURED || IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) { return -EACCES; }
    return status_watchdog_error();
}

/* 调用方持有锁；释放锁后执行策略，定时器中断超时也遵循此顺序。 */
static bool task_trip(struct status_task *task)
{
    if (task_monitor_stopped()) { return false; }
    task->state.flags = STATUS_TASK_FAULT;
    tasks.fault = (struct status_fault_snapshot){
        .task = task->state, .occurred_ms = k_uptime_get(),
        .error = -ETIMEDOUT, .feed_error = 0,
    };
    k_timer_stop(&task_timer);
    return true;
}

static void task_timer_expired(struct k_timer *expired_timer)
{
    ARG_UNUSED(expired_timer);
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    struct status_task *first = earliest_task();
    bool expired_task = first && first->deadline_ticks <= k_uptime_ticks() && task_trip(first);
    if (!task_monitor_stopped()) { schedule_next_task(); }
    k_spin_unlock(&status_runtime.lock, key);
    if (expired_task) { task_expired(); }
}

int status_register(struct status_task *task, const struct status_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = task_service_error();
    if (!err && (task->state.flags & STATUS_TASK_ACTIVE)) {
        err = -EALREADY;
    }
    if (!err) {
        task->state = (struct status_task_snapshot){
            .name = config->name, .timeout_ms = config->timeout_ms,
            .last_feed_ms = k_uptime_get(), .flags = STATUS_TASK_ACTIVE,
        };
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(config->timeout_ms);
        task->next = tasks.active;
        tasks.active = task;
        schedule_next_task();
    }
    k_spin_unlock(&status_runtime.lock, key);
    return err;
}

int status_feed(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = task_service_error();
    bool expired_task = false;
    if (!err && !(task->state.flags & STATUS_TASK_ACTIVE)) {
        err = -EACCES;
    }
    if (!err && k_uptime_ticks() >= task->deadline_ticks) {
        expired_task = task_trip(task);
        err = -ETIMEDOUT;
    }
    if (!err) {
        task->state.last_feed_ms = k_uptime_get();
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(task->state.timeout_ms);
        schedule_next_task();
    }
    k_spin_unlock(&status_runtime.lock, key);
    if (expired_task) { task_expired(); }
    return err;
}

int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot)
{
    if (!task || !snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = task->state.flags ? 0 : -ENOENT;
    if (!err) { *snapshot = task->state; }
    k_spin_unlock(&status_runtime.lock, key);
    return err;
}

int status_fault_get(struct status_fault_snapshot *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = tasks.fault.task.name ? 0 : -ENOENT;
    if (!err) { *snapshot = tasks.fault; }
    k_spin_unlock(&status_runtime.lock, key);
    if (!err) { snapshot->feed_error = status_watchdog_error(); }
    return err;
}

int status_sleep_forever(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = task_service_error();
    bool expired_task = false;
    if (!err && !task->state.flags) { err = -EACCES; }
    if (!err && (task->state.flags & STATUS_TASK_ACTIVE)) {
        if (k_uptime_ticks() >= task->deadline_ticks) {
            expired_task = task_trip(task);
            err = -ETIMEDOUT;
        } else {
            struct status_task **link = &tasks.active;
            while (*link != task) { link = &(*link)->next; }
            *link = task->next;
            task->next = NULL;
            task->state.flags = STATUS_TASK_SLEEP_FOREVER;
            schedule_next_task();
        }
    }
    k_spin_unlock(&status_runtime.lock, key);
    if (expired_task) { task_expired(); }
    return err;
}
