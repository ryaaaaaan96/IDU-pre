#include "status/status_led/status_led.h"
#include "status/status.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <errno.h>

static const struct gpio_dt_spec run_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static bool led_ready;
static int64_t next_toggle_ms;
static void update(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(status_led, update);

int status_led_init(void)
{
    if (!gpio_is_ready_dt(&run_led)) {
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(&run_led, GPIO_OUTPUT_ACTIVE);
    led_ready = !err;
    return err;
}

static void update(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!led_ready) {
        return;
    }
    status_phase_t phase = status_phase_get();
    int err = 0;
    if (phase == STATUS_RUN) {
        int64_t now = k_uptime_get();
        if (now >= next_toggle_ms) {
            /* 按错过的周期数决定翻转，保持既有调度抖动语义。 */
            int64_t periods = (now - next_toggle_ms) / STATUS_LED_HALF_PERIOD_MS + 1;
            next_toggle_ms += periods * STATUS_LED_HALF_PERIOD_MS;
            if (periods & 1) {
                err = gpio_pin_toggle_dt(&run_led);
            }
        }
    } else {
        err = gpio_pin_set_dt(&run_led, 1);
    }
    if (err && phase != STATUS_FAULT_LOCKED) {
        status_fault_phase_t fault_phase = phase == STATUS_STARTING ? STATUS_INIT : STATUS_RUNTIME;
        (void)status_report_fault(fault_phase, err, "Indicator output failed",
                                 fault_phase == STATUS_INIT ? STATUS_ACTION_HOLD : STATUS_ACTION_RESET);
    }
    /* 输出期间可能被其他线程或驱动回调锁存故障，当轮就转为常亮。 */
    if (phase != STATUS_FAULT_LOCKED && status_phase_get() == STATUS_FAULT_LOCKED) {
        (void)gpio_pin_set_dt(&run_led, 1);
    }
    k_work_reschedule(&status_led, K_MSEC(STATUS_LED_HALF_PERIOD_MS));
}

void status_led_start(void)
{
    next_toggle_ms = k_uptime_get() + STATUS_LED_HALF_PERIOD_MS;
    /* 不推迟故障路径已经提交的立即更新。 */
    k_work_schedule(&status_led, K_MSEC(STATUS_LED_HALF_PERIOD_MS));
}

void status_led_refresh(void)
{
    if (led_ready) {
        k_work_reschedule(&status_led, K_NO_WAIT);
    }
}
