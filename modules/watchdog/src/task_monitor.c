#include <watchdog/task_monitor.h>
#include <zephyr/kernel.h>
#include <errno.h>

/* ==================== 任务期限监控 ==================== */

static struct k_spinlock monitor_lock;
static bool initialized, stopped;
static watchdog_task_timeout_cb_t timeout_notify;

static struct {
    struct watchdog_task *active;
    struct watchdog_task_fault fault;
} tasks;
static void task_timer_expired(struct k_timer *timer);
static K_TIMER_DEFINE(task_timer, task_timer_expired, NULL);

int watchdog_task_monitor_init(watchdog_task_timeout_cb_t notify)
{
    if (!notify) { return -EINVAL; }
    if (!IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE)) { return -EACCES; }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = initialized ? -EALREADY : stopped ? -ECANCELED : 0;
    if (!err) { timeout_notify = notify; initialized = true; }
    k_spin_unlock(&monitor_lock, key);
    return err;
}

void watchdog_task_monitor_stop(void)
{
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    stopped = true;
    k_timer_stop(&task_timer);
    k_spin_unlock(&monitor_lock, key);
}

static void task_expired(void)
{
    timeout_notify();
}

/* 调用方持有锁。
 * 任务快照也用于封闭“已确定超时、尚未在锁外执行故障策略”的短暂窗口。 */
static bool task_monitor_stopped(void)
{
    return stopped || tasks.fault.task.name;
}

/* 调用方持有监控锁。 */
static struct watchdog_task *earliest_task(void)
{
    struct watchdog_task *first = NULL;
    for (struct watchdog_task *task = tasks.active; task; task = task->next) {
        if (!first || task->deadline_ticks < first->deadline_ticks) {
            first = task;
        }
    }
    return first;
}

/* 用一个单次定时器跟踪最早期限。 */
static void schedule_next_task(void)
{
    struct watchdog_task *next = earliest_task();
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
    if (!initialized) { return -EACCES; }
    return 0;
}

/* 调用方持有锁；释放锁后执行策略，定时器中断超时也遵循此顺序。 */
static bool task_trip(struct watchdog_task *task)
{
    if (task_monitor_stopped()) { return false; }
    task->state.flags = WATCHDOG_TASK_FAULT;
    tasks.fault = (struct watchdog_task_fault){
        .task = task->state, .occurred_ms = k_uptime_get(),
        .error = -ETIMEDOUT,
    };
    k_timer_stop(&task_timer);
    return true;
}

static void task_timer_expired(struct k_timer *expired_timer)
{
    ARG_UNUSED(expired_timer);
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    struct watchdog_task *first = earliest_task();
    bool expired_task = first && first->deadline_ticks <= k_uptime_ticks() && task_trip(first);
    if (!task_monitor_stopped()) { schedule_next_task(); }
    k_spin_unlock(&monitor_lock, key);
    if (expired_task) { task_expired(); }
}

int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = task_service_error();
    if (!err && (task->state.flags & WATCHDOG_TASK_ACTIVE)) {
        err = -EALREADY;
    }
    if (!err) {
        task->state = (struct watchdog_task_snapshot){
            .name = config->name, .timeout_ms = config->timeout_ms,
            .last_feed_ms = k_uptime_get(), .flags = WATCHDOG_TASK_ACTIVE,
        };
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(config->timeout_ms);
        task->next = tasks.active;
        tasks.active = task;
        schedule_next_task();
    }
    k_spin_unlock(&monitor_lock, key);
    return err;
}

int watchdog_task_feed(struct watchdog_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = task_service_error();
    bool expired_task = false;
    if (!err && !(task->state.flags & WATCHDOG_TASK_ACTIVE)) {
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
    k_spin_unlock(&monitor_lock, key);
    if (expired_task) { task_expired(); }
    return err;
}

int watchdog_task_get(const struct watchdog_task *task, struct watchdog_task_snapshot *snapshot)
{
    if (!task || !snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = task->state.flags ? 0 : -ENOENT;
    if (!err) { *snapshot = task->state; }
    k_spin_unlock(&monitor_lock, key);
    return err;
}

int watchdog_task_fault_get(struct watchdog_task_fault *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = tasks.fault.task.name ? 0 : -ENOENT;
    if (!err) { *snapshot = tasks.fault; }
    k_spin_unlock(&monitor_lock, key);
    return err;
}

int watchdog_task_sleep_forever(struct watchdog_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor_lock);
    int err = task_service_error();
    bool expired_task = false;
    if (!err && !task->state.flags) { err = -EACCES; }
    if (!err && (task->state.flags & WATCHDOG_TASK_ACTIVE)) {
        if (k_uptime_ticks() >= task->deadline_ticks) {
            expired_task = task_trip(task);
            err = -ETIMEDOUT;
        } else {
            struct watchdog_task **link = &tasks.active;
            while (*link != task) { link = &(*link)->next; }
            *link = task->next;
            task->next = NULL;
            task->state.flags = WATCHDOG_TASK_SLEEP_FOREVER;
            schedule_next_task();
        }
    }
    k_spin_unlock(&monitor_lock, key);
    if (expired_task) { task_expired(); }
    return err;
}
