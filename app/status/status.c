#include "status/status.h"
#include "task_watchdog/internal.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <dataprocess/params.h>
#include "status/image_confirm/image_confirm.h"
#endif
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <watchdog/watchdog.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);
/* Application lifecycle adapter; status-only tests supply this boundary. */
void protocol_stop(void);
static const struct led_dt_spec run_led = LED_DT_SPEC_GET(DT_ALIAS(led0));
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
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
} application;
static bool indicator_on;
static int64_t next_toggle_ms;
static void indicate(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(indicator, indicate);

static const enum status_action policies[STATUS_FAULT_COUNT] = {
    [STATUS_FAULT_NONE] = STATUS_RESET,
    [STATUS_FAULT_INPUT_INIT] = CONFIG_APP_FAULT_INPUT_INIT_ACTION,
    [STATUS_FAULT_IAP_INIT] = CONFIG_APP_FAULT_IAP_INIT_ACTION,
    [STATUS_FAULT_PROTOCOL_INIT] = CONFIG_APP_FAULT_PROTOCOL_INIT_ACTION,
    [STATUS_FAULT_SAMPLE] = CONFIG_APP_FAULT_SAMPLE_ACTION,
    [STATUS_FAULT_PUBLISH] = CONFIG_APP_FAULT_PUBLISH_ACTION,
    [STATUS_FAULT_PROTOCOL] = CONFIG_APP_FAULT_PROTOCOL_ACTION,
    [STATUS_FAULT_LED] = CONFIG_APP_FAULT_LED_ACTION,
    [STATUS_FAULT_WATCHDOG] = STATUS_RESET,
    [STATUS_FAULT_IMAGE_CONFIRM] = CONFIG_APP_FAULT_IMAGE_CONFIRM_ACTION,
    [STATUS_FAULT_TASK_TIMEOUT] = CONFIG_APP_FAULT_TASK_TIMEOUT_ACTION,
};

/* Reboot is a hardware boundary. Returning is supported by test adapters. */
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
    int err = application.fault.first == STATUS_FAULT_NONE ? -ENOENT : 0;
    if (!err) {
        *snapshot = application.fault;
    }
    k_spin_unlock(&lock, key);
    return err;
}

int status_report_fault(enum status_fault fault,
                        enum status_phase phase, int error)
{
    if (fault <= STATUS_FAULT_NONE || fault >= STATUS_FAULT_COUNT || !error) { return -EINVAL; }
    enum status_action action = policies[fault];
    if ((fault != STATUS_FAULT_LED && action == STATUS_CONTINUE) ||
        action > STATUS_RESET || fault == STATUS_FAULT_WATCHDOG) { action = STATUS_RESET; }
    if (action == STATUS_RESET && fault != STATUS_FAULT_WATCHDOG &&
        IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) { action = STATUS_HOLD; }
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (application.phase == STATUS_UNCONFIGURED) {
        k_spin_unlock(&lock, key);
        return -EACCES;
    }
    bool first = application.fault.first == STATUS_FAULT_NONE;
    if (first) {
        application.fault.first = fault;
        application.fault.phase = phase;
        application.fault.error = error;
        application.fault.occurred_ms = k_uptime_get();
    }
    bool terminal = action != STATUS_CONTINUE;
    bool act = terminal && (application.phase != STATUS_FAULT_LOCKED ||
                             (fault == STATUS_FAULT_WATCHDOG && application.fault.action != STATUS_RESET));
    if (act) {
        application.phase = STATUS_FAULT_LOCKED;
        application.fault.terminal = fault;
        application.fault.action = action;
    }
    k_spin_unlock(&lock, key);
    if (first || act) { LOG_ERR("Fault %d phase %d error %d action %d", fault, phase, error, action); }
    if (act) {
        protocol_stop();
#ifdef CONFIG_IDU_IAPV3
        iap_stop();
#endif
        if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
            status_tasks_halt(&monitor, action == STATUS_RESET);
        }
        k_work_reschedule(&indicator, K_NO_WAIT);
        if (action == STATUS_RESET) { status_request_reboot(); }
    }
    return terminal ? error : 0;
}

static void task_expired(void)
{
    (void)status_report_fault(STATUS_FAULT_TASK_TIMEOUT,
                              status_phase_get(), -ETIMEDOUT);
}

static int output(bool on)
{
    int err = !led_is_ready_dt(&run_led) ? -ENODEV :
              on ? led_on_dt(&run_led) : led_off_dt(&run_led);
    if (!err) { indicator_on = on; }
    return err;
}

static void indicate(struct k_work *work)
{
    ARG_UNUSED(work);
    enum status_phase phase = status_phase_get();
    if (phase == STATUS_UNCONFIGURED) { return; }
    int64_t now = k_uptime_get();
    bool on = true;
    if (phase >= STATUS_RUN && phase < STATUS_FAULT_LOCKED) {
        if (now >= next_toggle_ms) {
            int64_t periods = (now - next_toggle_ms) / STATUS_LED_HALF_PERIOD_MS + 1;
            on = (periods & 1) ? !indicator_on : indicator_on;
            next_toggle_ms += periods * STATUS_LED_HALF_PERIOD_MS;
        } else { on = indicator_on; }
    }
    int err = output(on);
    if (err && phase != STATUS_FAULT_LOCKED) {
        (void)status_report_fault(STATUS_FAULT_LED, phase, err);
    }
    if (status_phase_get() == STATUS_FAULT_LOCKED && !indicator_on) { (void)output(true); }
    /* Held faults still need a functioning external feed after main stops. */
    k_spinlock_key_t key = k_spin_lock(&lock);
    bool check_watchdog = application.fault.action != STATUS_RESET;
    k_spin_unlock(&lock, key);
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG) && check_watchdog) {
        err = watchdog_error(&monitor);
        if (err) { (void)status_report_fault(STATUS_FAULT_WATCHDOG, phase, err); }
    }
    k_work_reschedule(&indicator, K_MSEC(STATUS_LED_HALF_PERIOD_MS));
}

int status_init(void)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (application.phase != STATUS_UNCONFIGURED) {
        k_spin_unlock(&lock, key);
        return -EALREADY;
    }
    application.phase = STATUS_STARTING;
    k_spin_unlock(&lock, key);
    int err = 0;
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        err = watchdog_init(&monitor, &watchdog);
        if (!err) { err = status_tasks_init(&monitor, task_expired); }
        if (err) { return status_report_fault(STATUS_FAULT_WATCHDOG, STATUS_STARTING, err); }
    }
    err = output(true);
    return err ? status_report_fault(STATUS_FAULT_LED, STATUS_STARTING, err) : 0;
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
        if (err) { return status_report_fault(STATUS_FAULT_WATCHDOG, STATUS_CHECKING_HEALTH, err); }
    }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status parameters;
    dp_params_get_status(&parameters);
    bool ready = device_is_ready(flash) && (parameters.flags & DP_PARAM_STORAGE_READY) &&
                 !(parameters.flags & DP_PARAM_PENDING);
    int err = image_confirm_checkpoint(&application.image_confirm, k_uptime_get(), ready);
    if (err) { return status_report_fault(STATUS_FAULT_IMAGE_CONFIRM, STATUS_CHECKING_HEALTH, err); }
#endif
    return 0;
}
