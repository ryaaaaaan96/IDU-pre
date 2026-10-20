#include "status/idu_status.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "process/idu_process.h"
#endif
#include <health_monitor/health_monitor.h>
#include <led_indicator/led_indicator.h>

static const struct led_dt_spec run_led = LED_DT_SPEC_GET(DT_ALIAS(led0));
static const struct health_monitor_config watchdog = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
    .progress_timeout_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_health_ms),
};
static struct led_indicator indicator;
static struct health_monitor monitor;

int idu_status_start(struct idu_status *status)
{
    if (!status) { return -EINVAL; }
    if (status->phase != IDU_STATUS_UNCONFIGURED) { return -EALREADY; }
    status->phase = IDU_STATUS_STARTING;
    int err = health_monitor_start(&monitor, &watchdog);
    if (!err) { err = led_indicator_init(&indicator, &run_led); }
    if (!err) { err = led_indicator_set(&indicator, true); }
    if (err) { idu_status_fail(status, IDU_STATUS_STARTING, err); }
    return err;
}

void idu_status_set_phase(struct idu_status *status, enum idu_status_phase phase)
{
    if (status && status->phase != IDU_STATUS_UNCONFIGURED &&
        status->phase != IDU_STATUS_FAULT_LOCKED &&
        phase > IDU_STATUS_STARTING && phase < IDU_STATUS_FAULT_LOCKED) {
        status->phase = phase;
    }
}

int idu_status_progress(struct idu_status *status)
{
    if (!status) { return -EINVAL; }
    if (status->phase != IDU_STATUS_CHECKING_HEALTH) { return -EACCES; }
    /* Keep one toggle per healthy sample. Future state blink patterns belong
     * here; transient SAMPLING/PUBLISHING phases must not restart a pattern.
     * Check async indication errors before another output command clears them.
     */
    int err = led_indicator_error(&indicator);
    if (!err) { err = led_indicator_toggle(&indicator); }
    if (!err) { err = health_monitor_progress(&monitor); }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    if (!err) {
        err = idu_image_confirm_checkpoint(&status->image_confirm, k_uptime_get(),
                                           idu_equipment_confirm_ready());
    }
#endif
    return err;
}

void idu_status_fail(struct idu_status *status, enum idu_status_phase phase, int error)
{
    if (!status || status->phase == IDU_STATUS_FAULT_LOCKED) { return; }
    status->fault_phase = phase;
    status->last_error = error;
    status->phase = IDU_STATUS_FAULT_LOCKED;
    health_monitor_stop(&monitor);
    /* Best effort fault indication must not replace the original failure. */
    (void)led_indicator_set(&indicator, true);
}

uint32_t idu_status_timeout_ms(void)
{
    return watchdog.progress_timeout_ms;
}
