#include "status/inc/status.h"
#include "status_led.h"
#include <watchdog/watchdog.h>
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <dataprocess/params.h>
#include "image_confirm.h"
#endif
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);

/* ==================== 共享状态与后台工作声明 ==================== */

#define WATCHDOG_CHECK_INTERVAL_MS 100

void protocol_stop(void);
static struct {
    struct k_spinlock lock;
    status_phase_t phase;
} status_runtime;
static struct {
    struct status_application_fault fault;
    struct status_degradation degradation;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
} application;
static void maintain(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(maintenance, maintain);

/* ==================== 外部看门狗 ==================== */

static const struct watchdog_config watchdog_config = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
};
static struct watchdog monitor;
static bool watchdog_ready;

static int status_watchdog_init(void)
{
    if (IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        return 0;
    }
    int err = watchdog_init(&monitor, &watchdog_config);
    watchdog_ready = !err;
    return err;
}

static int status_watchdog_check(status_fault_phase_t phase)
{
    if (!watchdog_ready) {
        return 0;
    }
    int err = watchdog_error(&monitor);
    if (err) {
        return status_report_fault(phase, err, "Watchdog feed output failed", STATUS_ACTION_RESET);
    }
    return 0;
}

/* ==================== 任务期限监控 ==================== */

static struct {
    struct status_task *active;
    struct status_fault_snapshot fault;
} tasks;
static void task_timer_expired(struct k_timer *timer);
static K_TIMER_DEFINE(task_timer, task_timer_expired, NULL);

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
    return watchdog_error(&monitor);
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
    if (!err) { snapshot->feed_error = watchdog_error(&monitor); }
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

/* ==================== 整机状态、故障与健康检查 ==================== */

/* 复位属于硬件接口；测试适配实现允许调用返回。 */
__weak void status_request_reboot(void) { sys_reboot(SYS_REBOOT_COLD); }

status_phase_t status_phase_get(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    status_phase_t phase = status_runtime.phase;
    k_spin_unlock(&status_runtime.lock, key);
    return phase;
}

void status_update_degradation(uint16_t offline, uint16_t initializing, int storage_error)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    application.degradation.remote_offline = offline;
    application.degradation.remote_initializing = initializing;
    application.degradation.storage_error = storage_error;
    k_spin_unlock(&status_runtime.lock, key);
}

struct status_degradation status_degradation_get(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    struct status_degradation snapshot = application.degradation;
    k_spin_unlock(&status_runtime.lock, key);
    return snapshot;
}

int status_application_fault_get(struct status_application_fault *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    int err = !application.fault.description ? -ENOENT : 0;
    if (!err) {
        *snapshot = application.fault;
    }
    k_spin_unlock(&status_runtime.lock, key);
    return err;
}

int status_report_fault(status_fault_phase_t phase, int error,
                        const char *description, status_action_t action)
{
    if ((phase != STATUS_INIT && phase != STATUS_RUNTIME) || !error ||
        !description || !description[0] || (unsigned int)action > STATUS_ACTION_RESET ||
        (phase == STATUS_INIT && action == STATUS_ACTION_CONTINUE)) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase == STATUS_UNCONFIGURED) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EACCES;
    }
    application.fault.description = description;
    application.fault.phase = phase;
    application.fault.error = error;
    application.fault.occurred_ms = k_uptime_get();
    application.fault.action = action;
    if (action != STATUS_ACTION_CONTINUE) {
        status_runtime.phase = STATUS_FAULT_LOCKED;
        k_timer_stop(&task_timer);
    }
    k_spin_unlock(&status_runtime.lock, key);
    LOG_ERR("%s: phase %d error %d action %d", description, phase, error, action);
    switch (action) {
    case STATUS_ACTION_CONTINUE:
        return 0;
    case STATUS_ACTION_HOLD:
    case STATUS_ACTION_RESET:
        protocol_stop();
#ifdef CONFIG_IDU_IAPV3
        iap_stop();
#endif
        status_led_refresh();
        k_work_reschedule(&maintenance, K_NO_WAIT);
        if (action == STATUS_ACTION_RESET) {
            if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) { watchdog_stop(&monitor); }
            status_request_reboot();
        }
        return error;
    default:
        return -EINVAL;
    }
}

/* 故障留驻后主循环停止，此工作仍检查外部喂狗错误。 */
static void maintain(struct k_work *work)
{
    ARG_UNUSED(work);
    status_phase_t phase = status_phase_get();
    if (phase == STATUS_UNCONFIGURED) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    bool check_watchdog = application.fault.action != STATUS_ACTION_RESET;
    k_spin_unlock(&status_runtime.lock, key);
    if (check_watchdog) {
        (void)status_watchdog_check(phase == STATUS_STARTING ? STATUS_INIT : STATUS_RUNTIME);
    }
    k_work_reschedule(&maintenance, K_MSEC(WATCHDOG_CHECK_INTERVAL_MS));
}

int status_init(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase != STATUS_UNCONFIGURED) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EALREADY;
    }
    status_runtime.phase = STATUS_STARTING;
    k_spin_unlock(&status_runtime.lock, key);

    int err = status_watchdog_init();
    if (err) {
        return status_report_fault(STATUS_INIT, err, "Watchdog initialization failed", STATUS_ACTION_HOLD);
    }
    err = status_led_init();
    if (err) {
        return status_report_fault(STATUS_INIT, err, "Indicator initialization failed", STATUS_ACTION_HOLD);
    }
    return 0;
}

void status_set_phase(status_phase_t phase)
{
    bool start = false;
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase == STATUS_STARTING && phase == STATUS_RUN) {
        status_runtime.phase = STATUS_RUN;
        start = true;
    }
    k_spin_unlock(&status_runtime.lock, key);
    if (start) {
        status_led_start();
        k_work_reschedule(&maintenance, K_MSEC(WATCHDOG_CHECK_INTERVAL_MS));
    }
}

int status_progress(void)
{
    if (status_phase_get() != STATUS_RUN) { return -EACCES; }
    int err = status_watchdog_check(STATUS_RUNTIME);
    if (err) {
        return err;
    }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status parameters;
    dp_params_get_status(&parameters);
    bool ready = device_is_ready(flash) && (parameters.flags & DP_PARAM_STORAGE_READY) &&
                 !(parameters.flags & DP_PARAM_PENDING);
    err = image_confirm_checkpoint(&application.image_confirm, k_uptime_get(), ready);
    if (err) { return status_report_fault(STATUS_RUNTIME, err, "Image confirmation failed",
                                             STATUS_ACTION_RESET); }
#endif
    return 0;
}
