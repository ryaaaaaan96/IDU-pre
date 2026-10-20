#include "hardware/idu_pre.h"
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define IO_NODE DT_NODELABEL(idu_io)
static const struct gpio_dt_spec watchdog = GPIO_DT_SPEC_GET(IO_NODE, watchdog_gpios);
static atomic_t last_progress;
static atomic_t healthy;

static void feed_watchdog(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    uint32_t elapsed = k_uptime_get_32() - (uint32_t)atomic_get(&last_progress);
    if (atomic_get(&healthy) && elapsed < DT_PROP(IO_NODE, watchdog_health_ms)) {
        if (gpio_pin_toggle_dt(&watchdog)) {
            atomic_clear(&healthy);
        }
    }
}

K_TIMER_DEFINE(watchdog_timer, feed_watchdog, NULL);

int idu_health_init(void)
{
    if (!gpio_is_ready_dt(&watchdog)) {
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(&watchdog, GPIO_OUTPUT_ACTIVE);
    if (err) {
        return err;
    }
    atomic_set(&healthy, 1);
    idu_health_kick();
    k_timer_start(&watchdog_timer, K_MSEC(DT_PROP(IO_NODE, watchdog_feed_ms)),
                   K_MSEC(DT_PROP(IO_NODE, watchdog_feed_ms)));
    return 0;
}

void idu_health_kick(void)
{
    atomic_set(&last_progress, (atomic_val_t)k_uptime_get_32());
}

void idu_health_stop(void)
{
    atomic_clear(&healthy);
    k_timer_stop(&watchdog_timer);
}
