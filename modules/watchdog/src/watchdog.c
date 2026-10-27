#include <watchdog/watchdog.h>
#include <errno.h>

/* ==================== 外部 GPIO 喂狗 ==================== */

#ifdef CONFIG_WATCHDOG_EXTERNAL
static void feed(struct k_timer *timer)
{
    struct watchdog *monitor = CONTAINER_OF(timer, struct watchdog, timer);
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    if (monitor->active) {
        monitor->error = gpio_pin_toggle_dt(&monitor->config->feed);
        if (monitor->error) { monitor->active = false; }
    }
    bool active = monitor->active;
    k_spin_unlock(&monitor->lock, key);
    if (!active) { k_timer_stop(timer); }
}

int watchdog_init(struct watchdog *monitor,
                  const struct watchdog_config *config)
{
    if (!monitor || !config || !config->feed_interval_ms) { return -EINVAL; }
    if (monitor->initialized) { return -EALREADY; }
    if (!config->feed.port || !gpio_is_ready_dt(&config->feed)) { return -ENODEV; }
    const struct gpio_driver_config *port = config->feed.port->config;
    if (!port || config->feed.pin >= GPIO_MAX_PINS_PER_PORT ||
        !(port->port_pin_mask & BIT(config->feed.pin))) { return -EINVAL; }
    int err = gpio_pin_configure_dt(&config->feed, GPIO_OUTPUT_ACTIVE);
    if (err) { return err; }
    monitor->config = config;
    monitor->error = 0;
    monitor->active = true;
    k_timer_init(&monitor->timer, feed, NULL);
    monitor->initialized = true;
    k_timer_start(&monitor->timer, K_MSEC(config->feed_interval_ms),
                   K_MSEC(config->feed_interval_ms));
    return 0;
}

int watchdog_error(struct watchdog *monitor)
{
    if (!monitor) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    int err = monitor->initialized ? monitor->error : -EACCES;
    k_spin_unlock(&monitor->lock, key);
    return err;
}

void watchdog_stop(struct watchdog *monitor)
{
    if (!monitor) { return; }
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    bool initialized = monitor->initialized;
    monitor->active = false;
    if (!monitor->error) { monitor->error = -ECANCELED; }
    k_spin_unlock(&monitor->lock, key);
    if (initialized) { k_timer_stop(&monitor->timer); }
}

/* 板级配置仅在模块内解析，应用无需持有引脚或喂狗实例。 */
#define DT_DRV_COMPAT app_gpio_watchdog
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "Enable exactly one app,gpio-watchdog node");
BUILD_ASSERT(DT_INST_PROP(0, feed_interval_ms) > 0,
             "Watchdog feed interval must be positive");
static const struct watchdog_config external_config = {
    .feed = GPIO_DT_SPEC_INST_GET(0, feed_gpios),
    .feed_interval_ms = DT_INST_PROP(0, feed_interval_ms),
};
static struct watchdog external_monitor;
static int external_init_error = -EACCES;
static bool external_init_attempted;

int watchdog_external_init(void)
{
    /* 初始化由应用串行调用，每次上电仅尝试一次，失败后保留错误现场。 */
    if (external_init_attempted) { return -EALREADY; }
    external_init_attempted = true;
    external_init_error = watchdog_init(&external_monitor, &external_config);
    return external_init_error;
}

int watchdog_external_error(void)
{
    return external_init_error ? external_init_error : watchdog_error(&external_monitor);
}

void watchdog_external_stop(void)
{
    if (!external_init_attempted) {
        external_init_attempted = true;
        external_init_error = -ECANCELED;
    }
    watchdog_stop(&external_monitor);
}
#else
int watchdog_external_init(void) { return 0; }
int watchdog_external_error(void) { return 0; }
void watchdog_external_stop(void) {}
#endif

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

/* 线程接口在锁内维护有序链表；定时器只需查看表头。 */
static void task_insert(struct watchdog_task *task)
{
    struct watchdog_task **link = &tasks.active;
    while (*link && (*link)->deadline_ticks <= task->deadline_ticks) {
        link = &(*link)->next;
    }
    task->next = *link;
    *link = task;
}

/* 调用方已确认任务活动且持有监控锁。 */
static void task_remove(struct watchdog_task *task)
{
    struct watchdog_task **link = &tasks.active;
    while (*link != task) { link = &(*link)->next; }
    *link = task->next;
    task->next = NULL;
}

/* 用一个单次定时器跟踪最早期限。 */
static void schedule_next_task(void)
{
    struct watchdog_task *next = tasks.active;
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
    struct watchdog_task *first = tasks.active;
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
        task_insert(task);
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
        task_remove(task);
        task->state.last_feed_ms = k_uptime_get();
        task->deadline_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(task->state.timeout_ms);
        task_insert(task);
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
            task_remove(task);
            task->state.flags = WATCHDOG_TASK_SLEEP_FOREVER;
            schedule_next_task();
        }
    }
    k_spin_unlock(&monitor_lock, key);
    if (expired_task) { task_expired(); }
    return err;
}
