#include <led_indicator/led_indicator.h>
#include <limits.h>

static int drive(struct led_indicator *indicator, bool on)
{
    int err = on ? led_on_dt(&indicator->output) : led_off_dt(&indicator->output);
    indicator->error = err;
    if (!err) { indicator->lit = on; }
    return err;
}

static void blink_work(struct k_work *work)
{
    struct led_indicator *indicator =
        CONTAINER_OF(k_work_delayable_from_work(work), struct led_indicator, work);
    k_mutex_lock(&indicator->lock, K_FOREVER);
    if (indicator->blinking) {
        int64_t remaining = indicator->next_change - k_uptime_get();
        /* A callback from the previous pattern may already be running and
         * waiting for this lock when a new command sets a later deadline.
         */
        if (remaining > 0) {
            k_work_reschedule(&indicator->work, K_MSEC(remaining));
        } else if (drive(indicator, !indicator->lit)) {
            indicator->blinking = false;
        } else {
            uint32_t duration = indicator->lit ? indicator->on_ms : indicator->off_ms;
            indicator->next_change = k_uptime_get() + duration;
            k_work_reschedule(&indicator->work, K_MSEC(duration));
        }
    }
    k_mutex_unlock(&indicator->lock);
}

int led_indicator_init(struct led_indicator *indicator, const struct led_dt_spec *output)
{
    if (!indicator || !output) { return -EINVAL; }
    if (indicator->initialized) { return -EALREADY; }
    if (!output->dev || !led_is_ready_dt(output)) { return -ENODEV; }
    int err = led_off_dt(output);
    if (err) { return err; }
    indicator->output = *output;
    k_mutex_init(&indicator->lock);
    k_work_init_delayable(&indicator->work, blink_work);
    indicator->initialized = true;
    return 0;
}

static int steady(struct led_indicator *indicator, bool toggle, bool on)
{
    if (!indicator) { return -EINVAL; }
    if (!indicator->initialized) { return -EACCES; }
    k_mutex_lock(&indicator->lock, K_FOREVER);
    indicator->blinking = false;
    k_work_cancel_delayable(&indicator->work);
    int err = drive(indicator, toggle ? !indicator->lit : on);
    k_mutex_unlock(&indicator->lock);
    return err;
}

int led_indicator_set(struct led_indicator *indicator, bool on)
{
    return steady(indicator, false, on);
}

int led_indicator_toggle(struct led_indicator *indicator)
{
    return steady(indicator, true, false);
}

int led_indicator_blink(struct led_indicator *indicator, uint32_t on_ms, uint32_t off_ms)
{
    if (!indicator || !on_ms || !off_ms || on_ms > INT32_MAX || off_ms > INT32_MAX) {
        return -EINVAL;
    }
    if (!indicator->initialized) { return -EACCES; }
    k_mutex_lock(&indicator->lock, K_FOREVER);
    indicator->blinking = false;
    k_work_cancel_delayable(&indicator->work);
    int err = drive(indicator, true);
    if (!err) {
        indicator->on_ms = on_ms;
        indicator->off_ms = off_ms;
        indicator->blinking = true;
        indicator->next_change = k_uptime_get() + on_ms;
        k_work_reschedule(&indicator->work, K_MSEC(on_ms));
    }
    k_mutex_unlock(&indicator->lock);
    return err;
}

int led_indicator_error(struct led_indicator *indicator)
{
    if (!indicator) { return -EINVAL; }
    if (!indicator->initialized) { return -EACCES; }
    k_mutex_lock(&indicator->lock, K_FOREVER);
    int err = indicator->error;
    k_mutex_unlock(&indicator->lock);
    return err;
}
