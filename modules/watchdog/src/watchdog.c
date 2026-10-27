#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>
#include <zephyr/sys/atomic.h>
#ifdef CONFIG_WATCHDOG_EXTERNAL
#include <zephyr/drivers/gpio.h>
#define DT_DRV_COMPAT app_gpio_watchdog
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "Enable exactly one app,gpio-watchdog node");
BUILD_ASSERT(DT_INST_PROP(0, feed_interval_ms) > 0,
             "Watchdog feed interval must be positive");
static const struct gpio_dt_spec feed_gpio = GPIO_DT_SPEC_INST_GET(0, feed_gpios);
#endif

/* ==================== 共享健康状态 ==================== */

static struct k_spinlock lock;
/* 定时器只访问原子交接状态，不访问 monitor 或任务链表。 */
#define FEED_ENABLED BIT(0)
#define FEED_CHECKING BIT(1)
#define FEED_UNHEALTHY BIT(2)
#define FEED_GPIO_FAILED BIT(3)
static atomic_t feed_state;
static atomic_t feed_deadline_ms;
struct feed_event {
    int error;
    watchdog_fault_reason_t reason;
    int64_t occurred_ms;
};
/* 最多一个过期事件，以及 HOLD 后可能出现的一个 GPIO 故障。 */
K_MSGQ_DEFINE(feed_events, sizeof(struct feed_event), 2, 4);
/* 最近故障供统一查询，监控锁存仍独立决定健康状态和待通知事件。 */
static struct watchdog_fault latest_fault;
static struct {
    bool initialized;
    bool stopped;
    bool checking;
    bool notified;
    int64_t healthy_until_ms;
    struct watchdog_task *active;
    struct watchdog_fault fault;
    watchdog_fault_cb_t notify;
} monitor;
static void check_tasks(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(check_work, check_tasks);

static int64_t validity_ms(void)
{
#ifdef CONFIG_WATCHDOG_SOFTWARE
    return 3LL * CONFIG_WATCHDOG_CHECK_INTERVAL_MS;
#else
    return 0;
#endif
}

/* 调用方持锁。监控故障只锁存一次；新的 GPIO 失败可升级已留驻的任务故障。 */
static void record_fault(watchdog_fault_reason_t reason, int error,
                         const struct watchdog_task *task, int64_t occurred_ms)
{
    if (monitor.fault.error &&
        (reason != WATCHDOG_FAULT_FEED_GPIO || monitor.fault.reason == WATCHDOG_FAULT_FEED_GPIO)) {
        return;
    }
    atomic_or(&feed_state, FEED_UNHEALTHY);
    monitor.notified = false;
    monitor.fault = (struct watchdog_fault){
        .reason = reason, .error = error, .occurred_ms = occurred_ms,
    };
    switch (reason) {
    case WATCHDOG_FAULT_TASK: monitor.fault.description = "Task heartbeat timed out"; break;
    case WATCHDOG_FAULT_CHECK: monitor.fault.description = "Watchdog health check expired"; break;
    case WATCHDOG_FAULT_FEED_GPIO: monitor.fault.description = "Watchdog feed output failed"; break;
    default: break;
    }
    if (task) { monitor.fault.task = task->state; }
    latest_fault = monitor.fault;
}

/* 调用方持锁。合并定时器事件，记录和回调仍走原有监控路径。 */
static void collect_feed_events(void)
{
    struct feed_event event;
    while (k_msgq_get(&feed_events, &event, K_NO_WAIT) == 0) {
        record_fault(event.reason, event.error, NULL, event.occurred_ms);
    }
}

/* ==================== 硬件定时器：原子交接，不获取共享锁 ==================== */

#ifdef CONFIG_WATCHDOG_EXTERNAL
static void feed(struct k_timer *timer)
{
    atomic_val_t state = atomic_get(&feed_state);
    if (!(state & FEED_ENABLED) || (state & FEED_GPIO_FAILED) ||
        ((state & FEED_CHECKING) && (state & FEED_UNHEALTHY))) {
        return;
    }

    struct feed_event event = {.reason = WATCHDOG_FAULT_CHECK};
    event.error = gpio_pin_toggle_dt(&feed_gpio);
    if (event.error) {
        atomic_or(&feed_state, FEED_GPIO_FAILED);
        k_timer_stop(timer);
        event.reason = WATCHDOG_FAULT_FEED_GPIO;
    } else if ((atomic_get(&feed_state) & FEED_CHECKING) &&
               (int32_t)(k_uptime_get_32() - (uint32_t)atomic_get(&feed_deadline_ms)) >= 0) {
        /* 本轮先喂狗，再锁存过期；后续周期不再喂狗。 */
        atomic_or(&feed_state, FEED_UNHEALTHY);
        event.error = -ETIMEDOUT;
    } else {
        return;
    }

    event.occurred_ms = k_uptime_get();
    (void)k_msgq_put(&feed_events, &event, K_NO_WAIT);
    k_work_reschedule(&check_work, K_NO_WAIT);
}
static K_TIMER_DEFINE(feed_timer, feed, NULL);

static int configure_feed(void)
{
    if (!gpio_is_ready_dt(&feed_gpio)) { return -ENODEV; }
    const struct gpio_driver_config *port = feed_gpio.port->config;
    if (!port || feed_gpio.pin >= GPIO_MAX_PINS_PER_PORT ||
        !(port->port_pin_mask & BIT(feed_gpio.pin))) { return -EINVAL; }
    return gpio_pin_configure_dt(&feed_gpio, GPIO_OUTPUT_ACTIVE);
}
#endif

/* ==================== 工作队列：检查任务并发布有时效的健康结果 ==================== */

static void check_tasks(struct k_work *work)
{
    ARG_UNUSED(work);
    struct watchdog_fault fault;
    watchdog_fault_cb_t notify = NULL;
    k_spinlock_key_t key = k_spin_lock(&lock);
    collect_feed_events();
    if (monitor.checking && !monitor.fault.error) {
        int64_t now = k_uptime_get();
        if (now >= monitor.healthy_until_ms) {
            record_fault(WATCHDOG_FAULT_CHECK, -ETIMEDOUT, NULL, k_uptime_get());
        } else {
            struct watchdog_task *first = NULL;
            for (struct watchdog_task *task = monitor.active; task; task = task->next) {
                if (!first || task->deadline_ms < first->deadline_ms) { first = task; }
            }
            now = k_uptime_get();
            /* 只允许完整检查通过且旧结果尚未过期时续期。 */
            if (now >= monitor.healthy_until_ms) {
                record_fault(WATCHDOG_FAULT_CHECK, -ETIMEDOUT, NULL, k_uptime_get());
            } else if (first && first->deadline_ms <= now) {
                first->state.flags = WATCHDOG_TASK_FAULT;
                record_fault(WATCHDOG_FAULT_TASK, -ETIMEDOUT, first, now);
            } else {
                monitor.healthy_until_ms = now + validity_ms();
                atomic_set(&feed_deadline_ms, (uint32_t)monitor.healthy_until_ms);
            }
        }
    }
    if (monitor.fault.error && !monitor.notified) {
        monitor.notified = true;
        fault = monitor.fault;
        notify = monitor.notify;
    }
#ifdef CONFIG_WATCHDOG_SOFTWARE
    if (monitor.checking && !monitor.fault.error) {
        k_work_reschedule(&check_work, K_MSEC(CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    }
#endif
    k_spin_unlock(&lock, key);
    if (notify) { notify(&fault); }
}

/* ==================== 统一生命周期与复位 ==================== */

int watchdog_init(watchdog_fault_cb_t notify)
{
    if (!notify) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (monitor.initialized) {
        k_spin_unlock(&lock, key);
        return -EALREADY;
    }
    monitor.initialized = true;
    monitor.notify = notify;
    k_spin_unlock(&lock, key);
#ifdef CONFIG_WATCHDOG_EXTERNAL
    int err = configure_feed();
    if (err) {
        key = k_spin_lock(&lock);
        record_fault(WATCHDOG_FAULT_FEED_GPIO, err, NULL, k_uptime_get());
        monitor.notified = true; /* 初始化错误由返回值交给调用者处理。 */
        k_spin_unlock(&lock, key);
        return err;
    }
#endif
    key = k_spin_lock(&lock);
    if (monitor.stopped) {
        k_spin_unlock(&lock, key);
        return -ECANCELED;
    }
    monitor.checking = IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE);
    monitor.healthy_until_ms = k_uptime_get() + validity_ms();
#ifdef CONFIG_WATCHDOG_EXTERNAL
    atomic_set(&feed_deadline_ms, (uint32_t)monitor.healthy_until_ms);
    atomic_set(&feed_state, FEED_ENABLED | (monitor.checking ? FEED_CHECKING : 0));
    k_timer_start(&feed_timer, K_MSEC(DT_INST_PROP(0, feed_interval_ms)),
                  K_MSEC(DT_INST_PROP(0, feed_interval_ms)));
#endif
#ifdef CONFIG_WATCHDOG_SOFTWARE
    /* 首次工作立即提交；尚未执行时仅允许三倍检查周期的启动宽限。 */
    k_work_reschedule(&check_work, K_NO_WAIT);
#endif
    k_spin_unlock(&lock, key);
    return 0;
}

/* 主动上报独立于任务故障锁存，不能使 CONTINUE 停止硬件喂狗。 */
int watchdog_report_fault(int error,
                          const char *description, watchdog_action_t action)
{
    if (!error ||
        !description || !description[0] || (unsigned int)action > WATCHDOG_ACTION_RESET) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    watchdog_fault_cb_t notify = monitor.notify;
    if (!notify) {
        k_spin_unlock(&lock, key);
        return -EACCES;
    }
    collect_feed_events();
    const struct watchdog_fault fault = {
        .reason = WATCHDOG_FAULT_REPORTED,
        .error = error,
        .occurred_ms = k_uptime_get(),
        .action = action,
        .description = description,
    };
    latest_fault = fault;
    k_spin_unlock(&lock, key);
    return notify(&fault);
}

void watchdog_stop(watchdog_stop_mode_t mode)
{
    if (mode != WATCHDOG_STOP_MONITOR && mode != WATCHDOG_STOP_ALL) { return; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    monitor.initialized = true;
    monitor.stopped = true;
    monitor.checking = false;
    atomic_val_t clear = FEED_CHECKING | FEED_UNHEALTHY;
    if (mode == WATCHDOG_STOP_ALL) { clear |= FEED_ENABLED; }
    atomic_and(&feed_state, ~clear);
    (void)k_work_cancel_delayable(&check_work);
    if (mode == WATCHDOG_STOP_ALL) {
#ifdef CONFIG_WATCHDOG_EXTERNAL
        k_timer_stop(&feed_timer);
#endif
    }
    k_spin_unlock(&lock, key);
}

void watchdog_reset(void)
{
    watchdog_stop(WATCHDOG_STOP_ALL);
    sys_reboot(SYS_REBOOT_COLD);
}

/* ==================== 任务心跳与诊断快照 ==================== */

static int task_service_error(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    collect_feed_events();
    if (!IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE) || !monitor.initialized) { return -EACCES; }
    if (!monitor.checking || monitor.fault.error) { return -ECANCELED; }
    return 0;
}

/* 调用方持锁，迟到的心跳或退出请求不能掩盖任务超时；通知仍由工作队列执行。 */
static int task_deadline_check(struct watchdog_task *task)
{
    if (k_uptime_get() < task->deadline_ms) { return 0; }
    task->state.flags = WATCHDOG_TASK_FAULT;
    record_fault(WATCHDOG_FAULT_TASK, -ETIMEDOUT, task, k_uptime_get());
    k_work_reschedule(&check_work, K_NO_WAIT);
    return -ETIMEDOUT;
}

int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task_service_error();
    if (!err && (task->state.flags & WATCHDOG_TASK_ACTIVE)) { err = -EALREADY; }
    if (!err) {
        int64_t now = k_uptime_get();
        task->state = (struct watchdog_task_snapshot){
            .name = config->name, .timeout_ms = config->timeout_ms,
            .last_feed_ms = now, .flags = WATCHDOG_TASK_ACTIVE,
        };
        task->deadline_ms = now + config->timeout_ms;
        task->next = monitor.active;
        monitor.active = task;
    }
    k_spin_unlock(&lock, key);
    return err;
}

int watchdog_task_feed(struct watchdog_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task_service_error();
    if (!err && !(task->state.flags & WATCHDOG_TASK_ACTIVE)) { err = -EACCES; }
    if (!err) { err = task_deadline_check(task); }
    if (!err) {
        task->state.last_feed_ms = k_uptime_get();
        task->deadline_ms = task->state.last_feed_ms + task->state.timeout_ms;
    }
    k_spin_unlock(&lock, key);
    return err;
}

int watchdog_task_sleep_forever(struct watchdog_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task_service_error();
    if (!err && !task->state.flags) { err = -EACCES; }
    if (!err && (task->state.flags & WATCHDOG_TASK_ACTIVE)) {
        err = task_deadline_check(task);
        if (!err) {
            struct watchdog_task **link = &monitor.active;
            while (*link != task) { link = &(*link)->next; }
            *link = task->next;
            task->next = NULL;
            task->state.flags = WATCHDOG_TASK_SLEEP_FOREVER;
        }
    }
    k_spin_unlock(&lock, key);
    return err;
}

int watchdog_task_get(const struct watchdog_task *task, struct watchdog_task_snapshot *snapshot)
{
    if (!task || !snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task->state.flags ? 0 : -ENOENT;
    if (!err) { *snapshot = task->state; }
    k_spin_unlock(&lock, key);
    return err;
}

int watchdog_fault_get(struct watchdog_fault *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    collect_feed_events();
    int err = latest_fault.error ? 0 : -ENOENT;
    if (!err) { *snapshot = latest_fault; }
    k_spin_unlock(&lock, key);
    return err;
}
