#include "status/internal.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <dataprocess/params.h>
#include "status/image_confirm/image_confirm.h"
#endif
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);
void protocol_stop(void);
struct status_runtime status_runtime;
static struct {
    struct status_application_fault fault;
    struct status_degradation degradation;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
} application;
static void maintain(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(maintenance, maintain);

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
        status_tasks_stop();
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
        k_work_reschedule(&maintenance, K_NO_WAIT);
        if (action == STATUS_ACTION_RESET) {
            status_watchdog_stop();
            status_request_reboot();
        }
        return error;
    default:
        return -EINVAL;
    }
}

/* 故障留驻后主循环停止，此工作仍维护 LED 和外部喂狗诊断。 */
static void maintain(struct k_work *work)
{
    ARG_UNUSED(work);
    status_phase_t phase = status_phase_get();
    if (phase == STATUS_UNCONFIGURED) {
        return;
    }
    status_led_update(phase);

    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    bool check_watchdog = application.fault.action != STATUS_ACTION_RESET;
    k_spin_unlock(&status_runtime.lock, key);
    if (check_watchdog) {
        (void)status_watchdog_check(phase == STATUS_STARTING ? STATUS_INIT : STATUS_RUNTIME);
    }
    k_work_reschedule(&maintenance, K_MSEC(STATUS_LED_HALF_PERIOD_MS));
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
        status_led_start();
        start = true;
    }
    k_spin_unlock(&status_runtime.lock, key);
    if (start) { k_work_reschedule(&maintenance, K_MSEC(STATUS_LED_HALF_PERIOD_MS)); }
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
