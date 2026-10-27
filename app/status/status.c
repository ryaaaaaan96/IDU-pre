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

/* ==================== 状态维护与诊断查询 ==================== */

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

/* ==================== 故障记录与产品处理动作 ==================== */

static int status_watchdog_fault(const struct watchdog_fault *fault)
{
    status_phase_t current = status_phase_get();
    status_fault_phase_t phase = current == STATUS_STARTING ? STATUS_INIT : STATUS_RUNTIME;
    const char *description = fault->description;
    watchdog_action_t action = fault->action;
    int error = fault->error;

    if (fault->reason != WATCHDOG_FAULT_REPORTED) {
        /* 显式留驻后，只接受新的硬件故障。 */
        if (current == STATUS_FAULT_LOCKED && fault->reason != WATCHDOG_FAULT_GPIO) { return 0; }
        switch (fault->reason) {
        case WATCHDOG_FAULT_TASK: description = "Task heartbeat timed out"; break;
        case WATCHDOG_FAULT_CHECK: description = "Watchdog health check expired"; break;
        case WATCHDOG_FAULT_GPIO: description = "Watchdog feed output failed"; break;
        default: return -EINVAL;
        }
        action = phase == STATUS_INIT && fault->reason != WATCHDOG_FAULT_GPIO ?
                 WATCHDOG_ACTION_HOLD : WATCHDOG_ACTION_RESET;
    }
    if (phase == STATUS_INIT && action == WATCHDOG_ACTION_CONTINUE) { return -EINVAL; }
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
    if (action != WATCHDOG_ACTION_CONTINUE) {
        status_runtime.phase = STATUS_FAULT_LOCKED;
        watchdog_stop(action == WATCHDOG_ACTION_RESET ? WATCHDOG_STOP_ALL : WATCHDOG_STOP_MONITOR);
    }
    k_spin_unlock(&status_runtime.lock, key);
    /* CONTINUE 只记录，不解除此前已经锁存的状态或业务停止标志。 */
    if (action == WATCHDOG_ACTION_CONTINUE) {
        LOG_ERR("%s: phase %d error %d action %d", description, phase, error, action);
        return 0;
    }

    /* 关键动作不等待工作队列；RESET 已在锁内停喂，日志也不阻塞。 */
    protocol_stop();
#ifdef CONFIG_IDU_IAPV3
    iap_stop();
#endif
    LOG_ERR("%s: phase %d error %d action %d", description, phase, error, action);
    if (action == WATCHDOG_ACTION_RESET) { watchdog_reset(); }

    /* RESET 正常不返回；测试复位钩子返回时仍允许更新现场显示。 */
    status_led_refresh();
    return error;
}

/* ==================== 统一初始化 ==================== */

int status_init(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase != STATUS_UNCONFIGURED) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EALREADY;
    }
    status_runtime.phase = STATUS_STARTING;
    k_spin_unlock(&status_runtime.lock, key);

    int err = watchdog_init(status_watchdog_fault);
    if (err) {
        return watchdog_report_fault(err, "Watchdog initialization failed", WATCHDOG_ACTION_HOLD);
    }
    err = status_led_init();
    if (err) {
        return watchdog_report_fault(err, "Indicator initialization failed", WATCHDOG_ACTION_HOLD);
    }
    return 0;
}

/* ==================== 试运行镜像健康确认 ==================== */

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
    if (err) { return watchdog_report_fault(err, "Image confirmation failed",
                                             WATCHDOG_ACTION_RESET); }
#endif
    return 0;
}
