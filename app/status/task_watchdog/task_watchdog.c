#include "task_watchdog.h"
#include "internal.h"
#include <watchdog/watchdog.h>
#include <errno.h>
#include <zephyr/sys/reboot.h>

static struct k_spinlock lock;
static struct watchdog *physical;
static void (*on_expired)(void);
static bool initialized, terminal, fault_valid;
static struct status_fault_snapshot fault;
static struct status_task *active;
static struct k_timer timer;

/* Caller holds lock. A single one-shot timer follows the earliest deadline. */
static void schedule_next(void)
{
    struct status_task *next = NULL;
    for (struct status_task *task = active; task; task = task->next) {
        if (!next || task->deadline_ticks < next->deadline_ticks) { next = task; }
    }
    if (terminal || !next) { k_timer_stop(&timer); }
    else { k_timer_start(&timer, K_TIMEOUT_ABS_TICKS(next->deadline_ticks), K_NO_WAIT); }
}

static int service_error(void)
{
    if (terminal) { return -ECANCELED; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!initialized) { return -EACCES; }
    return watchdog_error(physical);
}

/* Caller holds lock. Policy runs after unlocking, including ISR expiry. */
static bool trip(struct status_task *task)
{
    if (terminal) { return false; }
    terminal = true;
    task->state.flags = STATUS_TASK_FAULT;
    fault = (struct status_fault_snapshot){
        .task = task->state, .occurred_ms = k_uptime_get(),
        .error = -ETIMEDOUT, .feed_error = 0,
    };
    fault_valid = true;
    k_timer_stop(&timer);
    return true;
}

static void expired(struct k_timer *expired_timer)
{
    ARG_UNUSED(expired_timer);
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct status_task *first = NULL;
    int64_t now = k_uptime_ticks();
    for (struct status_task *task = active; task; task = task->next) {
        if (task->deadline_ticks <= now &&
            (!first || task->deadline_ticks < first->deadline_ticks)) { first = task; }
    }
    bool reset = first && trip(first);
    if (!terminal) { schedule_next(); }
    k_spin_unlock(&lock, key);
    if (reset) { on_expired(); }
}

int status_tasks_init(struct watchdog *monitor, void (*expired_callback)(void))
{
    if (!monitor || !expired_callback) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = terminal ? -ECANCELED : initialized ? -EALREADY : 0;
    if (!err) {
        k_timer_init(&timer, expired, NULL);
        physical = monitor;
        on_expired = expired_callback;
        initialized = true;
    }
    k_spin_unlock(&lock, key);
    return err;
}

void status_tasks_halt(struct watchdog *monitor, bool stop_feeding)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    terminal = true;
    if (initialized) { k_timer_stop(&timer); }
    if (stop_feeding) { watchdog_stop(monitor); }
    k_spin_unlock(&lock, key);
}

int status_register(struct status_task *task, const struct status_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = service_error();
    if (!err && task->registered && (task->state.flags & STATUS_TASK_ACTIVE)) {
        err = -EALREADY;
    }
    if (!err) {
        task->state = (struct status_task_snapshot){
            .name = config->name, .timeout_ms = config->timeout_ms,
            .last_feed_ms = k_uptime_get(), .flags = STATUS_TASK_ACTIVE,
        };
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(config->timeout_ms);
        task->next = active;
        active = task;
        task->registered = true;
        schedule_next();
    }
    k_spin_unlock(&lock, key);
    return err;
}

int status_feed(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = service_error();
    bool reset = false;
    if (!err && (!task->registered || !(task->state.flags & STATUS_TASK_ACTIVE))) {
        err = -EACCES;
    }
    if (!err && k_uptime_ticks() >= task->deadline_ticks) {
        reset = trip(task);
        err = -ETIMEDOUT;
    }
    if (!err) {
        task->state.last_feed_ms = k_uptime_get();
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(task->state.timeout_ms);
        schedule_next();
    }
    k_spin_unlock(&lock, key);
    if (reset) { on_expired(); }
    return err;
}

int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot)
{
    if (!task || !snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task->registered ? 0 : -ENOENT;
    if (!err) { *snapshot = task->state; }
    k_spin_unlock(&lock, key);
    return err;
}

int status_fault_get(struct status_fault_snapshot *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = fault_valid ? 0 : -ENOENT;
    if (!err) { *snapshot = fault; snapshot->feed_error = watchdog_error(physical); }
    k_spin_unlock(&lock, key);
    return err;
}

int status_sleep_forever(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = service_error();
    bool reset = false;
    if (!err && !task->registered) { err = -EACCES; }
    if (!err && (task->state.flags & STATUS_TASK_ACTIVE)) {
        if (k_uptime_ticks() >= task->deadline_ticks) {
            reset = trip(task);
            err = -ETIMEDOUT;
        } else {
            struct status_task **link = &active;
            while (*link != task) { link = &(*link)->next; }
            *link = task->next;
            task->next = NULL;
            task->state.flags = STATUS_TASK_SLEEP_FOREVER;
            schedule_next();
        }
    }
    k_spin_unlock(&lock, key);
    if (reset) { on_expired(); }
    return err;
}
