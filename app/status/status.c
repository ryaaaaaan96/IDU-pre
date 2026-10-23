#include "status/status.h"
#include "task_watchdog/internal.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <dataprocess/params.h>
#endif
#include <watchdog/watchdog.h>
#include <zephyr/drivers/led.h>
#include <errno.h>

static const struct led_dt_spec run_led = LED_DT_SPEC_GET(DT_ALIAS(led0));
static const struct watchdog_config watchdog = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
};
static struct watchdog monitor;

#ifdef CONFIG_BOOTLOADER_MCUBOOT
/* 镜像确认由产品健康策略决定，不属于测点回调或采集驱动。 */
static bool parameters_ready(void)
{
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status status;

    dp_params_get_status(&status);
    return device_is_ready(flash) && (status.flags & DP_PARAM_STORAGE_READY) &&
           !(status.flags & DP_PARAM_PENDING);
}
#endif

static int set_run_led(struct application_status *status, bool on)
{
    int err = on ? led_on_dt(&run_led) : led_off_dt(&run_led);

    if (!err) {
        status->led_on = on;
    }
    return err;
}

int status_init(struct application_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status->phase != STATUS_UNCONFIGURED) {
        return -EALREADY;
    }
    status->phase = STATUS_STARTING;
    int err = 0;

    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        err = watchdog_init(&monitor, &watchdog);
        if (!err) { err = status_tasks_init(&monitor); }
    }

    if (!err && !led_is_ready_dt(&run_led)) {
        err = -ENODEV;
    }
    if (!err) {
        err = set_run_led(status, true);
    }
    if (err) {
        status_fail(status, STATUS_STARTING, err);
    }
    return err;
}

void status_set_phase(struct application_status *status, enum status_phase phase)
{
    if (status && status->phase != STATUS_UNCONFIGURED &&
        status->phase != STATUS_FAULT_LOCKED &&
        phase > STATUS_STARTING && phase < STATUS_FAULT_LOCKED) {
        status->phase = phase;
    }
}

int status_progress(struct application_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status->phase != STATUS_CHECKING_HEALTH) {
        return -EACCES;
    }
    /* 当前每次健康采样翻转一次；后续状态闪烁策略在此实现。 */
    int err = set_run_led(status, !status->led_on);
    if (!err && !IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        err = watchdog_error(&monitor);
    }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    if (!err) {
        err = image_confirm_checkpoint(&status->image_confirm, k_uptime_get(),
                                           parameters_ready());
    }
#endif
    return err;
}

void status_fail(struct application_status *status, enum status_phase phase, int error)
{
    if (!status || status->phase == STATUS_FAULT_LOCKED) {
        return;
    }
    status->fault_phase = phase;
    status->last_error = error;
    status->phase = STATUS_FAULT_LOCKED;
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        status_tasks_stop(&monitor);
    }
    /* Best effort fault indication must not replace the original failure. */
    if (led_is_ready_dt(&run_led)) {
        (void)set_run_led(status, true);
    }
}
