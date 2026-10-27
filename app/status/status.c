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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);

/* ==================== 整机状态与后台工作声明 ==================== */

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

/* ==================== 看门狗配置与故障策略 ==================== */

static void watchdog_fault(const struct watchdog_fault *fault)
{
    status_phase_t current = status_phase_get();
    /* 已显式留驻后只接受新的硬件故障，不用旧监控通知覆盖产品故障。 */
    if (current == STATUS_FAULT_LOCKED && fault->reason != WATCHDOG_FAULT_GPIO) { return; }
    status_fault_phase_t phase = current == STATUS_STARTING ? STATUS_INIT : STATUS_RUNTIME;
    const char *description;
    switch (fault->reason) {
    case WATCHDOG_FAULT_TASK: description = "Task heartbeat timed out"; break;
    case WATCHDOG_FAULT_CHECK: description = "Watchdog health check expired"; break;
    case WATCHDOG_FAULT_GPIO: description = "Watchdog feed output failed"; break;
    default: return;
    }
    status_action_t action = phase == STATUS_INIT && fault->reason != WATCHDOG_FAULT_GPIO ?
                             STATUS_ACTION_HOLD : STATUS_ACTION_RESET;
    (void)status_report_fault(phase, fault->error, description, action);
}

/* ==================== 整机状态、故障与健康检查 ==================== */

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
        watchdog_stop(action == STATUS_ACTION_RESET ? WATCHDOG_STOP_ALL : WATCHDOG_STOP_MONITOR);
    }
    k_spin_unlock(&status_runtime.lock, key);
    /* 关键动作先执行，不依赖日志后端、LED 工作或系统队列获得调度。 */
    if (action != STATUS_ACTION_CONTINUE) {
        protocol_stop();
#ifdef CONFIG_IDU_IAPV3
        iap_stop();
#endif
    }

    /* 使用 Zephyr 延后、非阻塞日志，只提交记录，不等待日志后端。 */
    LOG_ERR("%s: phase %d error %d action %d", description, phase, error, action);
    if (action == STATUS_ACTION_RESET) { watchdog_reset(); }

    /* RESET 正常不返回；测试复位钩子返回时仍允许更新现场显示。 */
    if (action != STATUS_ACTION_CONTINUE) {
        status_led_refresh();
    }
    return action == STATUS_ACTION_CONTINUE ? 0 : error;
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

    int err = watchdog_init(watchdog_fault);
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
    }
}

int status_progress(void)
{
    if (status_phase_get() != STATUS_RUN) { return -EACCES; }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status parameters;
    dp_params_get_status(&parameters);
    bool ready = device_is_ready(flash) && (parameters.flags & DP_PARAM_STORAGE_READY) &&
                 !(parameters.flags & DP_PARAM_PENDING);
    int err = image_confirm_checkpoint(&application.image_confirm, k_uptime_get(), ready);
    if (err) { return status_report_fault(STATUS_RUNTIME, err, "Image confirmation failed",
                                             STATUS_ACTION_RESET); }
#endif
    return 0;
}
