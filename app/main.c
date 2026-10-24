#include "acquisition/acquisition.h"
#include "process/points.h"
#include "protocol/inc/protocol.h"
#include "status/status.h"
#include <acquisition/sample.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif

int startup(void)
{
    int err = status_init();
    if (err) { return err; }
    enum status_fault fault = STATUS_FAULT_INPUT_INIT;

    err = acquisition_inputs_init();
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
        if (status_phase_get() != STATUS_FAULT_LOCKED) {
            (void)status_report_fault(fault, STATUS_STARTING, err);
        }
        return err;
    }

    status_set_phase(STATUS_RUN);
    return 0;
}

int main(void)
{
    int err = startup();

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + points_sample_period_ms();
        err = acquisition_cycle();
        if (err) {
            return err;
        }
        /* An overrun starts the next cycle without replaying missed samples. */
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
