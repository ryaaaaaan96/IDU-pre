#include "acquisition/acquisition.h"
#include "process/points.h"
#include "protocol/inc/protocol.h"
#include "status/status.h"
#include <acquisition/sample.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int application_startup(struct application_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status->phase != STATUS_UNCONFIGURED) {
        return -EALREADY;
    }

    int err = status_init(status);

    if (!err) {
        err = acquisition_inputs_init();
    }
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_start();
    }
#endif
    if (!err) {
        err = protocol_init();
    }
    if (err) {
        status_fail(status, STATUS_STARTING, err);
        LOG_ERR("Startup failed: %d; watchdog feeding stopped", err);
        return err;
    }

    status_set_phase(status, STATUS_RUN);
    return 0;
}

int main(void)
{
    struct application_status status = {0};
    int err = application_startup(&status);

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + points_sample_period_ms();
        err = acquisition_cycle(&status);
        if (err) {
            return err;
        }
        int64_t delay = next_sample - k_uptime_get();

        if (delay > 0) {
            k_sleep(K_MSEC(delay));
        }
    }
}
