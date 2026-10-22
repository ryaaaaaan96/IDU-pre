#include "acquisition/idu_acquisition.h"
#include "process/idu_process.h"
#include "protocol/inc/protocol.h"
#include "status/idu_status.h"
#include <gpio/idu_inputs.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_main, LOG_LEVEL_INF);

int idu_startup(struct idu_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status->phase != IDU_STATUS_UNCONFIGURED) {
        return -EALREADY;
    }

    int err = idu_status_start(status);

    if (!err) {
        err = idu_inputs_init();
    }
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_start();
    }
#endif
    if (!err) {
        err = idu_protocol_init();
    }
    if (err) {
        idu_status_fail(status, IDU_STATUS_STARTING, err);
        LOG_ERR("Startup failed: %d; watchdog feeding stopped", err);
        return err;
    }

    idu_status_set_phase(status, IDU_STATUS_WAITING);
    return 0;
}

int main(void)
{
    struct idu_status status = {0};
    int err = idu_startup(&status);

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + idu_sample_period_ms();
        err = idu_acquisition_cycle(&status);
        if (err) {
            return err;
        }
        int64_t delay = next_sample - k_uptime_get();

        if (delay > 0) {
            k_sleep(K_MSEC(delay));
        }
    }
}
