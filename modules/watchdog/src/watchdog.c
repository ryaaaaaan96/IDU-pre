#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>
#ifdef CONFIG_WATCHDOG_EXTERNAL
#include <zephyr/drivers/gpio.h>
#define DT_DRV_COMPAT app_gpio_watchdog
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "Enable exactly one app,gpio-watchdog node");
BUILD_ASSERT(DT_INST_PROP(0, feed_interval_ms) > 0,
             "Watchdog feed interval must be positive");
static const struct gpio_dt_spec feed_gpio = GPIO_DT_SPEC_INST_GET(0, feed_gpios);
#endif

/* ==================== 运行控制与诊断 ==================== */

/* 模式是任务检查和喂狗许可的唯一来源，线程与中断使用同一套规则。 */
typedef enum {
    WATCHDOG_UNINITIALIZED,
    WATCHDOG_MONITORING,
    WATCHDOG_FAULTED,
    WATCHDOG_HOLDING,
    WATCHDOG_RESET_PENDING,
    WATCHDOG_STOPPED,
} watchdog_mode_t;

#define RESET_DELAY_MAX_MS (60U * 1000U)
#define WATCHDOG_VALIDITY_MS (3U * CONFIG_WATCHDOG_CHECK_INTERVAL_MS)

static atomic_t mode;
static atomic_t gpio_failed; /* GPIO 失败不因 HOLD 或延时复位而恢复。 */
static atomic_t healthy_until_ms;
static struct k_spinlock lock; /* 只保护任务、回调绑定、诊断和复位期限。 */
static struct watchdog_task *active_tasks;
static watchdog_fault_cb_t fault_callback;
static int64_t reset_deadline_ms;

/* 序号只用于诊断排序，避免旧的排队通知覆盖后来的业务上报。 */
struct fault_event {
    uint32_t sequence;
    struct watchdog_fault fault;
};
static atomic_t fault_sequence;
static struct fault_event latest_fault;
/* 一次监控故障，以及随后可能出现的一次 GPIO 故障。 */
K_MSGQ_DEFINE(fault_events, sizeof(struct fault_event), 2, 4);
static void check_tasks(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(check_work, check_tasks);
static void request_reset(uint32_t delay_ms);

static bool health_expired(void)
{
    return (int32_t)(k_uptime_get_32() - (uint32_t)atomic_get(&healthy_until_ms)) >= 0;
}

static int handle_fault(const struct watchdog_fault *fault)
{
    int err = fault_callback(fault); /* 不持锁，先维护状态、关闭业务并提交日志。 */
    if (fault->action == WATCHDOG_ACTION_HOLD) {
        watchdog_stop(WATCHDOG_STOP_MONITOR);
    } else if (fault->action == WATCHDOG_ACTION_RESET) {
        request_reset(fault->reset_delay_ms);
    }
    return err;
}

/* 调用方保护任务对象；定时器只传 NULL。模式锁存防止重复入队和健康恢复。
 * 这里只入队；调用方释放模块锁后再安排检查工作。
 */
static bool queue_fault(watchdog_fault_reason_t reason, int error,
                        struct watchdog_task *task)
{
    if (reason == WATCHDOG_FAULT_FEED_GPIO) {
        if (!atomic_cas(&gpio_failed, 0, 1)) { return false; }
        (void)atomic_cas(&mode, WATCHDOG_MONITORING, WATCHDOG_FAULTED);
    } else if (!atomic_cas(&mode, WATCHDOG_MONITORING, WATCHDOG_FAULTED)) {
        return false;
    }
    static const char *const descriptions[] = {
        [WATCHDOG_FAULT_TASK] = "Task heartbeat timed out",
        [WATCHDOG_FAULT_CHECK] = "Watchdog health check expired",
        [WATCHDOG_FAULT_FEED_GPIO] = "Watchdog feed output failed",
    };
    struct fault_event event = {
        .sequence = (uint32_t)atomic_inc(&fault_sequence) + 1U,
        .fault = {
            .reason = reason, .error = error, .occurred_ms = k_uptime_get(),
            .description = descriptions[reason],
            .action = WATCHDOG_ACTION_RESET, .reset_delay_ms = 200,
        },
    };
    if (task) {
        task->state.flags = WATCHDOG_TASK_FAULT;
        event.fault.task = task->state;
    }
    (void)k_msgq_put(&fault_events, &event, K_NO_WAIT);
    return true;
}

/* ==================== 硬件定时器：只判断喂狗许可并操作 GPIO ==================== */

#ifdef CONFIG_WATCHDOG_EXTERNAL
static void feed(struct k_timer *timer)
{
    watchdog_mode_t current = atomic_get(&mode);
    if (atomic_get(&gpio_failed) ||
        (current != WATCHDOG_MONITORING && current != WATCHDOG_HOLDING &&
         current != WATCHDOG_RESET_PENDING)) {
        return;
    }
    int err = gpio_pin_toggle_dt(&feed_gpio);
    if (err) {
        queue_fault(WATCHDOG_FAULT_FEED_GPIO, err, NULL);
        k_work_reschedule(&check_work, K_NO_WAIT);
        k_timer_stop(timer);
    } else if (current == WATCHDOG_MONITORING && health_expired()) {
        /* 本轮先喂狗，再锁存过期；下一周期不再喂狗。 */
        queue_fault(WATCHDOG_FAULT_CHECK, -ETIMEDOUT, NULL);
        k_work_reschedule(&check_work, K_NO_WAIT);
    }
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

/* ==================== 工作队列：检查任务，消费故障事件 ==================== */

static void check_tasks(struct k_work *work)
{
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (atomic_get(&mode) == WATCHDOG_MONITORING) {
        struct watchdog_task *first = NULL;
        for (struct watchdog_task *task = active_tasks; task; task = task->next) {
            if (!first || task->deadline_ms < first->deadline_ms) { first = task; }
        }
        int64_t now = k_uptime_get();
        if (health_expired()) {
            queue_fault(WATCHDOG_FAULT_CHECK, -ETIMEDOUT, NULL);
        } else if (first && first->deadline_ms <= now) {
            queue_fault(WATCHDOG_FAULT_TASK, -ETIMEDOUT, first);
        } else if (atomic_get(&mode) == WATCHDOG_MONITORING) {
            atomic_set(&healthy_until_ms, (uint32_t)(now + WATCHDOG_VALIDITY_MS));
        }
    }
    k_spin_unlock(&lock, key);

    struct fault_event event;
    for (;;) {
        /* 出队与诊断更新不可分开，避免查询漏掉正在交接的故障。 */
        key = k_spin_lock(&lock);
        int err = k_msgq_get(&fault_events, &event, K_NO_WAIT);
        if (err) {
            k_spin_unlock(&lock, key);
            break;
        }
        if ((int32_t)(event.sequence - latest_fault.sequence) > 0) { latest_fault = event; }
        k_spin_unlock(&lock, key);
        watchdog_mode_t current = atomic_get(&mode);
        bool notify = current == WATCHDOG_FAULTED ||
            (event.fault.reason == WATCHDOG_FAULT_FEED_GPIO &&
             (current == WATCHDOG_HOLDING || current == WATCHDOG_RESET_PENDING));
        if (notify) { handle_fault(&event.fault); }
    }
    if (atomic_get(&mode) == WATCHDOG_MONITORING) {
        k_work_reschedule(&check_work, K_MSEC(CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    }
}

/* ==================== 初始化、故障上报和停止 ==================== */

int watchdog_init(watchdog_fault_cb_t notify)
{
    if (!notify) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (fault_callback || atomic_get(&mode) != WATCHDOG_UNINITIALIZED) {
        k_spin_unlock(&lock, key);
        return -EALREADY;
    }
    fault_callback = notify; /* 绑定后不替换，失败也不重新初始化。 */
    k_spin_unlock(&lock, key);
#ifdef CONFIG_WATCHDOG_EXTERNAL
    int err = configure_feed();
    if (err) {
        atomic_set(&gpio_failed, 1);
        atomic_set(&mode, WATCHDOG_STOPPED);
        key = k_spin_lock(&lock);
        latest_fault = (struct fault_event){
            .sequence = (uint32_t)atomic_inc(&fault_sequence) + 1U,
            .fault = {
                .reason = WATCHDOG_FAULT_FEED_GPIO, .error = err,
                .occurred_ms = k_uptime_get(), .description = "Watchdog feed output failed",
            },
        };
        k_spin_unlock(&lock, key);
        return err; /* 初始化失败交给调用者处理，不排队重复通知。 */
    }
#endif
    key = k_spin_lock(&lock);
    atomic_set(&healthy_until_ms, k_uptime_get_32() + WATCHDOG_VALIDITY_MS);
    if (!atomic_cas(&mode, WATCHDOG_UNINITIALIZED, WATCHDOG_MONITORING)) {
        k_spin_unlock(&lock, key);
        return -ECANCELED;
    }
#ifdef CONFIG_WATCHDOG_EXTERNAL
    k_timer_start(&feed_timer, K_MSEC(DT_INST_PROP(0, feed_interval_ms)),
                  K_MSEC(DT_INST_PROP(0, feed_interval_ms)));
#endif
    k_work_reschedule(&check_work, K_NO_WAIT);
    k_spin_unlock(&lock, key);
    return 0;
}

int watchdog_report_fault(int error, const char *description,
                          watchdog_action_t action, uint32_t reset_delay_ms)
{
    if (!error || !description || !description[0] ||
        (unsigned int)action > WATCHDOG_ACTION_RESET) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (!fault_callback) {
        k_spin_unlock(&lock, key);
        return -EACCES;
    }
    struct fault_event event = {
        .sequence = (uint32_t)atomic_inc(&fault_sequence) + 1U,
        .fault = {
            .reason = WATCHDOG_FAULT_REPORTED, .error = error,
            .occurred_ms = k_uptime_get(), .description = description,
            .action = action, .reset_delay_ms = reset_delay_ms,
        },
    };
    latest_fault = event;
    k_spin_unlock(&lock, key);
    return handle_fault(&event.fault);
}

void watchdog_stop(watchdog_stop_mode_t stop)
{
    if (stop != WATCHDOG_STOP_MONITOR && stop != WATCHDOG_STOP_ALL) { return; }
    if (stop == WATCHDOG_STOP_ALL) {
        atomic_set(&mode, WATCHDOG_STOPPED);
#ifdef CONFIG_WATCHDOG_EXTERNAL
        k_timer_stop(&feed_timer);
#endif
    } else {
        atomic_val_t current;
        do {
            current = atomic_get(&mode);
            if (current == WATCHDOG_STOPPED || current == WATCHDOG_RESET_PENDING) { return; }
        } while (!atomic_cas(&mode, current, WATCHDOG_HOLDING));
    }
    (void)k_work_cancel_delayable(&check_work);
}

/* ==================== 私有复位：线程与中断使用同一路径 ==================== */

static void reset_expired(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    watchdog_stop(WATCHDOG_STOP_ALL);
    sys_reboot(SYS_REBOOT_COLD);
}
static K_TIMER_DEFINE(reset_timer, reset_expired, NULL);

static void request_reset(uint32_t delay_ms)
{
    if (!delay_ms) { reset_expired(NULL); return; }
    atomic_val_t current;
    do {
        current = atomic_get(&mode);
        if (current == WATCHDOG_STOPPED) { break; }
    } while (!atomic_cas(&mode, current, WATCHDOG_RESET_PENDING));
    (void)k_work_cancel_delayable(&check_work);
    k_spinlock_key_t key = k_spin_lock(&lock);
    int64_t now = k_uptime_get();
    int64_t deadline = now + MIN(delay_ms, RESET_DELAY_MAX_MS);
    if (!reset_deadline_ms || deadline < reset_deadline_ms) {
        reset_deadline_ms = deadline;
        k_timer_start(&reset_timer, K_MSEC((uint32_t)(deadline - now)), K_NO_WAIT);
    }
    k_spin_unlock(&lock, key);
}

/* ==================== 任务心跳与只读诊断 ==================== */

static int task_service_error(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!fault_callback) { return -EACCES; }
    return atomic_get(&mode) == WATCHDOG_MONITORING ? 0 : -ECANCELED;
}

static int task_deadline_check(struct watchdog_task *task)
{
    if (k_uptime_get() < task->deadline_ms) { return 0; }
    return queue_fault(WATCHDOG_FAULT_TASK, -ETIMEDOUT, task) ? -ETIMEDOUT : -ECANCELED;
}

int watchdog_task_register(struct watchdog_task *task, const struct watchdog_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) { return -EINVAL; }
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
        task->next = active_tasks;
        active_tasks = task;
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
    if (err == -ETIMEDOUT) { k_work_reschedule(&check_work, K_NO_WAIT); }
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
            struct watchdog_task **next = &active_tasks;
            while (*next != task) { next = &(*next)->next; }
            *next = task->next;
            task->state.flags = WATCHDOG_TASK_SLEEP_FOREVER;
            task->next = NULL;
        }
    }
    k_spin_unlock(&lock, key);
    if (err == -ETIMEDOUT) { k_work_reschedule(&check_work, K_NO_WAIT); }
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
    struct fault_event newest = latest_fault, pending;
    /* 只查看未处理事件，不消费、不改变运行模式，也不触发通知。 */
    for (uint32_t i = 0; k_msgq_peek_at(&fault_events, &pending, i) == 0; ++i) {
        if ((int32_t)(pending.sequence - newest.sequence) > 0) { newest = pending; }
    }
    int err = newest.fault.error ? 0 : -ENOENT;
    if (!err) { *snapshot = newest.fault; }
    k_spin_unlock(&lock, key);
    return err;
}
