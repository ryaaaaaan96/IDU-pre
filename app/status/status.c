#include "status/idu_status.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "process/idu_process.h"
#endif
#include <watchdog/watchdog.h>
#include <zephyr/drivers/led.h>
#include <errno.h>

static const struct led_dt_spec run_led = LED_DT_SPEC_GET(DT_ALIAS(led0));
static const struct watchdog_config watchdog = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
    .progress_timeout_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_health_ms),
};
static struct watchdog monitor;

static int set_run_led(struct idu_status *status, bool on)
{
    int err = on ? led_on_dt(&run_led) : led_off_dt(&run_led);
    if (!err) { status->led_on = on; }
    return err;
}

int idu_status_start(struct idu_status *status)
{
    if (!status) { return -EINVAL; }
    if (status->phase != IDU_STATUS_UNCONFIGURED) { return -EALREADY; }
    status->phase = IDU_STATUS_STARTING;
    int err = watchdog_start(&monitor, &watchdog);
    if (!err && !led_is_ready_dt(&run_led)) { err = -ENODEV; }
    if (!err) { err = set_run_led(status, true); }
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
    /* 当前每次健康采样翻转一次；后续状态闪烁策略在此实现。 */
    int err = set_run_led(status, !status->led_on);
    if (!err) { err = watchdog_progress(&monitor); }
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
    watchdog_stop(&monitor);
    /* Best effort fault indication must not replace the original failure. */
    if (led_is_ready_dt(&run_led)) { (void)set_run_led(status, true); }
}

uint32_t idu_status_timeout_ms(void)
{
    return watchdog.progress_timeout_ms;
}
