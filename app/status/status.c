#include "status/status.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <dataprocess/params.h>
#include "status/image_confirm/image_confirm.h"
#endif
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);
/* 应用生命周期适配接口；仅测试状态模块时由测试提供实现。 */
void protocol_stop(void);
static const struct gpio_dt_spec run_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct watchdog_config watchdog = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
};
static struct watchdog monitor;
static struct k_spinlock lock;
static struct {
    enum status_phase phase;
    struct status_application_fault fault;
    struct status_degradation degradation;
    struct status_task *active;
    struct status_fault_snapshot task_fault;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
} application;
static void task_timer_expired(struct k_timer *timer);
static K_TIMER_DEFINE(task_timer, task_timer_expired, NULL);
static bool indicator_ready;
static int64_t next_toggle_ms;
static void indicate(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(indicator, indicate);

/* 复位属于硬件接口；测试适配实现允许调用返回。 */
__weak void status_request_reboot(void) { sys_reboot(SYS_REBOOT_COLD); }

enum status_phase status_phase_get(void)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    enum status_phase phase = application.phase;
    k_spin_unlock(&lock, key);
    return phase;
}

void status_update_degradation(uint16_t offline, uint16_t initializing, int storage_error)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    application.degradation.remote_offline = offline;
    application.degradation.remote_initializing = initializing;
    application.degradation.storage_error = storage_error;
    k_spin_unlock(&lock, key);
}

struct status_degradation status_degradation_get(void)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct status_degradation snapshot = application.degradation;
    k_spin_unlock(&lock, key);
    return snapshot;
}

int status_application_fault_get(struct status_application_fault *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = !application.fault.description ? -ENOENT : 0;
    if (!err) {
        *snapshot = application.fault;
    }
    k_spin_unlock(&lock, key);
    return err;
}

int status_report_fault(enum status_fault_phase phase, int error,
                        const char *description, enum status_action action)
{
    if ((phase != STATUS_INIT && phase != STATUS_RUNTIME) || !error ||
        !description || !description[0] || (unsigned int)action > STATUS_RESET ||
        (phase == STATUS_INIT && action == STATUS_CONTINUE)) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (application.phase == STATUS_UNCONFIGURED) {
        k_spin_unlock(&lock, key);
        return -EACCES;
    }
    bool first = !application.fault.description;
    if (first) {
        application.fault.description = description;
        application.fault.phase = phase;
        application.fault.error = error;
        application.fault.occurred_ms = k_uptime_get();
    }
    bool upgrade = action > application.fault.action;
    if (upgrade) {
        application.phase = STATUS_FAULT_LOCKED;
        application.fault.terminal_description = description;
        application.fault.action = action;
        k_timer_stop(&task_timer);
    }
    k_spin_unlock(&lock, key);
    if (first || upgrade) { LOG_ERR("%s: phase %d error %d action %d", description, phase, error, action); }
    if (upgrade) {
        protocol_stop();
#ifdef CONFIG_IDU_IAPV3
        iap_stop();
#endif
        if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG) && action == STATUS_RESET) {
            watchdog_stop(&monitor);
        }
        k_work_reschedule(&indicator, K_NO_WAIT);
        if (action == STATUS_RESET) { status_request_reboot(); }
    }
    if (action != STATUS_CONTINUE) { return error; }
    return 0;
}

static void task_expired(void)
{
    enum status_fault_phase phase = STATUS_RUNTIME;
    if (status_phase_get() == STATUS_STARTING) { phase = STATUS_INIT; }
    (void)status_report_fault(phase, -ETIMEDOUT, "Task heartbeat timed out",
                              CONFIG_APP_FAULT_TASK_TIMEOUT_ACTION);
}

/* 调用方持有锁。
 * 任务快照也用于封闭“已确定超时、尚未在锁外执行故障策略”的短暂窗口。 */
static bool task_monitor_stopped(void)
{
    return application.phase == STATUS_FAULT_LOCKED || application.task_fault.task.name;
}

/* 调用方持有锁；用一个单次定时器跟踪最早期限。 */
static void schedule_next_task(void)
{
    struct status_task *next = NULL;
    for (struct status_task *task = application.active; task; task = task->next) {
        if (!next || task->deadline_ticks < next->deadline_ticks) { next = task; }
    }
    if (task_monitor_stopped() || !next) { k_timer_stop(&task_timer); }
    else { k_timer_start(&task_timer, K_TIMEOUT_ABS_TICKS(next->deadline_ticks), K_NO_WAIT); }
}

static int task_service_error(void)
{
    if (task_monitor_stopped()) { return -ECANCELED; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (application.phase == STATUS_UNCONFIGURED || IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) { return -EACCES; }
    return watchdog_error(&monitor);
}

/* 调用方持有锁；释放锁后执行策略，定时器中断超时也遵循此顺序。 */
static bool task_trip(struct status_task *task)
{
    if (task_monitor_stopped()) { return false; }
    task->state.flags = STATUS_TASK_FAULT;
    application.task_fault = (struct status_fault_snapshot){
        .task = task->state, .occurred_ms = k_uptime_get(),
        .error = -ETIMEDOUT, .feed_error = 0,
    };
    k_timer_stop(&task_timer);
    return true;
}

static void task_timer_expired(struct k_timer *expired_timer)
{
    ARG_UNUSED(expired_timer);
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct status_task *first = NULL;
    int64_t now = k_uptime_ticks();
    for (struct status_task *task = application.active; task; task = task->next) {
        if (task->deadline_ticks <= now &&
            (!first || task->deadline_ticks < first->deadline_ticks)) { first = task; }
    }
    bool expired_task = first && task_trip(first);
    if (!task_monitor_stopped()) { schedule_next_task(); }
    k_spin_unlock(&lock, key);
    if (expired_task) { task_expired(); }
}

int status_register(struct status_task *task, const struct status_task_config *config)
{
    if (!task || !config || !config->name || !config->name[0] || !config->timeout_ms) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
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
        task->next = application.active;
        application.active = task;
        schedule_next_task();
    }
    k_spin_unlock(&lock, key);
    return err;
}

int status_feed(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
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
    k_spin_unlock(&lock, key);
    if (expired_task) { task_expired(); }
    return err;
}

int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot)
{
    if (!task || !snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task->state.flags ? 0 : -ENOENT;
    if (!err) { *snapshot = task->state; }
    k_spin_unlock(&lock, key);
    return err;
}

int status_fault_get(struct status_fault_snapshot *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = application.task_fault.task.name ? 0 : -ENOENT;
    if (!err) { *snapshot = application.task_fault; }
    k_spin_unlock(&lock, key);
    if (!err) { snapshot->feed_error = watchdog_error(&monitor); }
    return err;
}

int status_sleep_forever(struct status_task *task)
{
    if (!task) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    int err = task_service_error();
    bool expired_task = false;
    if (!err && !task->state.flags) { err = -EACCES; }
    if (!err && (task->state.flags & STATUS_TASK_ACTIVE)) {
        if (k_uptime_ticks() >= task->deadline_ticks) {
            expired_task = task_trip(task);
            err = -ETIMEDOUT;
        } else {
            struct status_task **link = &application.active;
            while (*link != task) { link = &(*link)->next; }
            *link = task->next;
            task->next = NULL;
            task->state.flags = STATUS_TASK_SLEEP_FOREVER;
            schedule_next_task();
        }
    }
    k_spin_unlock(&lock, key);
    if (expired_task) { task_expired(); }
    return err;
}

static void indicate(struct k_work *work)
{
    ARG_UNUSED(work);
    enum status_phase phase = status_phase_get();
    if (phase == STATUS_UNCONFIGURED) { return; }
    enum status_fault_phase fault_phase = STATUS_RUNTIME;
    if (phase == STATUS_STARTING) { fault_phase = STATUS_INIT; }
    if (indicator_ready) {
        int err = 0;
        if (phase >= STATUS_RUN && phase < STATUS_FAULT_LOCKED) {
            int64_t now = k_uptime_get();
            if (now >= next_toggle_ms) {
                int64_t periods = (now - next_toggle_ms) / STATUS_LED_HALF_PERIOD_MS + 1;
                next_toggle_ms += periods * STATUS_LED_HALF_PERIOD_MS;
                if (periods & 1) { err = gpio_pin_toggle_dt(&run_led); }
            }
        } else { err = gpio_pin_set_dt(&run_led, 1); }
        if (err && phase != STATUS_FAULT_LOCKED) {
            (void)status_report_fault(fault_phase, err, "Indicator output failed",
                                      CONFIG_APP_FAULT_LED_ACTION);
        }
    }
    /* 一次读取故障阶段和动作；main 停止后，留驻仍需检查外部喂狗。 */
    k_spinlock_key_t key = k_spin_lock(&lock);
    bool force_on = indicator_ready && phase != STATUS_FAULT_LOCKED &&
                    application.phase == STATUS_FAULT_LOCKED;
    bool check_watchdog = application.fault.action != STATUS_RESET;
    k_spin_unlock(&lock, key);
    if (force_on) { (void)gpio_pin_set_dt(&run_led, 1); }
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG) && check_watchdog) {
        int err = watchdog_error(&monitor);
        if (err) { (void)status_report_fault(fault_phase, err, "Watchdog feed output failed", STATUS_RESET); }
    }
    k_work_reschedule(&indicator, K_MSEC(STATUS_LED_HALF_PERIOD_MS));
}

int status_init(void)
{
    if (application.phase != STATUS_UNCONFIGURED) {
        return -EALREADY;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    application.phase = STATUS_STARTING;
    k_spin_unlock(&lock, key);
    int err = 0;
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        err = watchdog_init(&monitor, &watchdog);
        if (err) { return status_report_fault(STATUS_INIT, err, "Watchdog initialization failed", STATUS_RESET); }
    }
    if (!gpio_is_ready_dt(&run_led)) {
        return status_report_fault(STATUS_INIT, -ENODEV, "Indicator initialization failed",
                                   CONFIG_APP_FAULT_LED_INIT_ACTION);
    }
    err = gpio_pin_configure_dt(&run_led, GPIO_OUTPUT_ACTIVE);
    if (err) {
        return status_report_fault(STATUS_INIT, err, "Indicator initialization failed",
                                   CONFIG_APP_FAULT_LED_INIT_ACTION);
    }
    indicator_ready = true;
    return 0;
}

void status_set_phase(enum status_phase phase)
{
    bool start = false;
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (application.phase != STATUS_UNCONFIGURED && application.phase != STATUS_FAULT_LOCKED &&
        phase > STATUS_STARTING && phase < STATUS_FAULT_LOCKED) {
        if (application.phase == STATUS_STARTING) {
            start = true;
            next_toggle_ms = k_uptime_get() + STATUS_LED_HALF_PERIOD_MS;
        }
        application.phase = phase;
    }
    k_spin_unlock(&lock, key);
    if (start) { k_work_reschedule(&indicator, K_MSEC(STATUS_LED_HALF_PERIOD_MS)); }
}

int status_progress(void)
{
    if (status_phase_get() != STATUS_CHECKING_HEALTH) { return -EACCES; }
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        int err = watchdog_error(&monitor);
        if (err) { return status_report_fault(STATUS_RUNTIME, err, "Watchdog feed output failed", STATUS_RESET); }
    }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status parameters;
    dp_params_get_status(&parameters);
    bool ready = device_is_ready(flash) && (parameters.flags & DP_PARAM_STORAGE_READY) &&
                 !(parameters.flags & DP_PARAM_PENDING);
    int err = image_confirm_checkpoint(&application.image_confirm, k_uptime_get(), ready);
    if (err) { return status_report_fault(STATUS_RUNTIME, err, "Image confirmation failed",
                                             CONFIG_APP_FAULT_IMAGE_CONFIRM_ACTION); }
#endif
    return 0;
}
