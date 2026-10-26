#include "status/internal.h"
#include <watchdog/watchdog.h>

static const struct watchdog_config config = {
    .feed = GPIO_DT_SPEC_GET(DT_NODELABEL(idu_io), watchdog_gpios),
    .feed_interval_ms = DT_PROP(DT_NODELABEL(idu_io), watchdog_feed_ms),
};
static struct watchdog monitor;
static bool ready;

int status_watchdog_init(void)
{
    if (IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        return 0;
    }
    int err = watchdog_init(&monitor, &config);
    ready = !err;
    return err;
}

void status_watchdog_stop(void)
{
    if (!IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG)) {
        watchdog_stop(&monitor);
    }
}

int status_watchdog_error(void)
{
    return watchdog_error(&monitor);
}

int status_watchdog_check(status_fault_phase_t phase)
{
    if (!ready) {
        return 0;
    }
    int err = status_watchdog_error();
    if (err) {
        return status_report_fault(phase, err, "Watchdog feed output failed", STATUS_ACTION_RESET);
    }
    return 0;
}
