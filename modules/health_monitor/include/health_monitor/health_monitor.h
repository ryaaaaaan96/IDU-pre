#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

struct health_monitor_config {
    struct gpio_dt_spec feed;
    uint32_t feed_interval_ms;
    uint32_t progress_timeout_ms;
};

struct health_monitor {
    struct k_timer timer;
    struct k_spinlock lock;
    const struct health_monitor_config *config;
    uint32_t last_progress;
    int error;
    bool initialized;
    bool active;
};

/* Zero-initialize each instance once. The instance and const config must outlive
 * the timer. Serialize startup; progress/stop are safe against timer execution.
 * The feed GPIO must support ISR-safe toggle (on-chip GPIO, not a sleeping bus).
 * Timeout, GPIO failure and explicit stop are terminal until the device resets.
 * Stop leaves the pin driven at its last level so the external watchdog expires.
 */
int health_monitor_start(struct health_monitor *monitor,
                         const struct health_monitor_config *config);
int health_monitor_progress(struct health_monitor *monitor);
void health_monitor_stop(struct health_monitor *monitor);

#endif
