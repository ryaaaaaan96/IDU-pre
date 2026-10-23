#include "task_watchdog.h"
#include "internal.h"
#include <watchdog/watchdog.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <errno.h>
#include <zephyr/sys/reboot.h>

static struct k_spinlock lock;
static struct watchdog *physical;
static bool initialized, terminal, fault_valid;
static struct status_fault_snapshot fault;

static int service_error(void)
{
    if (terminal) { return -ECANCELED; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!initialized) { return -EACCES; }
    return watchdog_error(physical);
}

/* Caller holds lock. Stop the physical backend before requesting reset. */
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
    watchdog_stop(physical);
    return true;
}

static void expired(int channel, void *user)
{
    struct status_task *task = user;
    k_spinlock_key_t key = k_spin_lock(&lock);
    bool reset = task->registered && task->channel == channel &&
        (task->state.flags & STATUS_TASK_ACTIVE) &&
        k_uptime_ticks() >= task->deadline_ticks && trip(task);
    k_spin_unlock(&lock, key);
    if (reset) { sys_reboot(SYS_REBOOT_COLD); }
}

int status_tasks_init(struct watchdog *monitor)
{
    if (!monitor) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = terminal ? -ECANCELED : initialized ? -EALREADY : task_wdt_init(NULL);
    if (!err) { physical = monitor; initialized = true; }
    k_spin_unlock(&lock, key);
    return err;
}

void status_tasks_stop(struct watchdog *monitor)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    terminal = true;
    watchdog_stop(monitor);
    k_spin_unlock(&lock, key);
}

int status_register(struct status_task *task, const struct status_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = service_error();
    if (!err && task->registered) { err = -EALREADY; }
    if (!err) {
        int64_t now = k_uptime_get();
        int64_t due = k_uptime_ticks() + k_ms_to_ticks_ceil64(config->timeout_ms);
        int channel = task_wdt_add(config->timeout_ms, expired, task);
        if (channel < 0) { err = channel; }
        else {
            task->state = (struct status_task_snapshot){
                .name = config->name, .timeout_ms = config->timeout_ms,
                .last_feed_ms = now, .flags = STATUS_TASK_ACTIVE,
            };
            task->deadline_ticks = due;
            task->channel = channel;
            task->registered = true;
        }
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
        int64_t now = k_uptime_get();
        int64_t due = k_uptime_ticks() + k_ms_to_ticks_ceil64(task->state.timeout_ms);
        err = task_wdt_feed(task->channel);
        if (!err) { task->state.last_feed_ms = now; task->deadline_ticks = due; }
    }
    k_spin_unlock(&lock, key);
    if (reset) { sys_reboot(SYS_REBOOT_COLD); }
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
