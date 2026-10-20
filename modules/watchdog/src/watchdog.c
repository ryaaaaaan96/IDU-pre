#include <watchdog/watchdog.h>
#include <errno.h>
#include <limits.h>

static bool expired(struct watchdog *monitor)
{
    return (uint32_t)(k_uptime_get_32() - monitor->last_progress) >=
           monitor->config->progress_timeout_ms;
}

static void feed(struct k_timer *timer)
{
    struct watchdog *monitor = CONTAINER_OF(timer, struct watchdog, timer);
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    if (monitor->active) {
        monitor->error = expired(monitor) ? -ETIMEDOUT : gpio_pin_toggle_dt(&monitor->config->feed);
        if (monitor->error) { monitor->active = false; }
    }
    bool active = monitor->active;
    k_spin_unlock(&monitor->lock, key);
    if (!active) { k_timer_stop(timer); }
}

int watchdog_start(struct watchdog *monitor,
                   const struct watchdog_config *config)
{
    if (!monitor || !config || !config->feed_interval_ms ||
        config->progress_timeout_ms <= config->feed_interval_ms ||
        config->progress_timeout_ms > INT32_MAX) { return -EINVAL; }
    if (monitor->initialized) { return -EALREADY; }
    if (!config->feed.port || !gpio_is_ready_dt(&config->feed)) { return -ENODEV; }
    const struct gpio_driver_config *port = config->feed.port->config;
    if (!port || config->feed.pin >= GPIO_MAX_PINS_PER_PORT ||
        !(port->port_pin_mask & BIT(config->feed.pin))) { return -EINVAL; }
    int err = gpio_pin_configure_dt(&config->feed, GPIO_OUTPUT_ACTIVE);
    if (err) { return err; }
    monitor->config = config;
    monitor->last_progress = k_uptime_get_32();
    monitor->error = 0;
    monitor->active = true;
    k_timer_init(&monitor->timer, feed, NULL);
    monitor->initialized = true;
    k_timer_start(&monitor->timer, K_MSEC(config->feed_interval_ms),
                   K_MSEC(config->feed_interval_ms));
    return 0;
}

int watchdog_progress(struct watchdog *monitor)
{
    if (!monitor) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    if (!monitor->initialized) {
        k_spin_unlock(&monitor->lock, key);
        return -EACCES;
    }
    /* A late cycle must not re-enable feeding, even before the next timer tick. */
    if (monitor->active && expired(monitor)) {
        monitor->active = false;
        monitor->error = -ETIMEDOUT;
    }
    int err = monitor->error;
    if (monitor->active) { monitor->last_progress = k_uptime_get_32(); }
    k_spin_unlock(&monitor->lock, key);
    if (err) { k_timer_stop(&monitor->timer); }
    return err;
}

void watchdog_stop(struct watchdog *monitor)
{
    if (!monitor) { return; }
    k_spinlock_key_t key = k_spin_lock(&monitor->lock);
    bool initialized = monitor->initialized;
    monitor->active = false;
    if (!monitor->error) { monitor->error = -ECANCELED; }
    k_spin_unlock(&monitor->lock, key);
    if (initialized) { k_timer_stop(&monitor->timer); }
}
