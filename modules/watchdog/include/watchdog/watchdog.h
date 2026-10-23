#ifndef WATCHDOG_MONITOR_H
#define WATCHDOG_MONITOR_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

struct watchdog_config {
    struct gpio_dt_spec feed;
    uint32_t feed_interval_ms;
};

struct watchdog {
    struct k_timer timer;
    struct k_spinlock lock;
    const struct watchdog_config *config;
    int error;
    bool initialized;
    bool active;
};

/* Zero-initialize each instance once. The instance and const config must outlive
 * the timer. Serialize initialization; error query/stop are safe against timer execution.
 * The feed GPIO must support ISR-safe toggle (on-chip GPIO, not a sleeping bus).
 * GPIO failure and explicit stop are terminal until the device resets.
 * Stop leaves the pin driven at its last level so the external watchdog expires.
 */
int watchdog_init(struct watchdog *monitor,
                  const struct watchdog_config *config);
int watchdog_error(struct watchdog *monitor);
void watchdog_stop(struct watchdog *monitor);

#endif
