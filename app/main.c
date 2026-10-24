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

int startup(struct application_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status_phase_get(status) != STATUS_UNCONFIGURED) {
        return -EALREADY;
    }

    int err = status_init(status);
    enum status_fault fault = STATUS_FAULT_INPUT_INIT;

    if (!err) {
        err = acquisition_inputs_init();
    }
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_init();
        fault = STATUS_FAULT_IAP_INIT;
    }
#endif
    if (!err) {
        err = protocol_init();
        fault = STATUS_FAULT_PROTOCOL_INIT;
    }
    if (err) {
        if (status_phase_get(status) != STATUS_FAULT_LOCKED) {
            (void)status_report_fault(status, fault, STATUS_STARTING, err);
        }
        LOG_ERR("Startup failed: %d", err);
        return err;
    }

    status_set_phase(status, STATUS_RUN);
    return 0;
}

int main(void)
{
    static struct application_status status;
    int err = startup(&status);

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + points_sample_period_ms();
        err = acquisition_cycle(&status);
        if (err) {
            return err;
        }
        /* An overrun starts the next cycle without replaying missed samples. */
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
