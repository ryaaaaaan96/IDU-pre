#include "acquisition/acquisition.h"
#include "protocol/inc/protocol.h"
#include "process/points.h"
#include <dataprocess/points.h>
#include <acquisition/sample.h>
#include <transmit/forward.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_acquisition, LOG_LEVEL_INF);

static int lock_fault(struct application_status *status,
                      enum status_fault fault, enum status_phase phase, int error)
{
    int result = status_report_fault(status, fault, phase, error);
    LOG_ERR("Acquisition fault in phase %d: %d",
            status->fault_phase, status->last_error);
    return result;
}

int acquisition_cycle(struct application_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status_phase_get(status) != STATUS_RUN) {
        return -EACCES;
    }

    status_set_phase(status, STATUS_SAMPLING);
    struct acquisition_sample sample = {0};
    int err = acquisition_inputs_sample(&sample);

    sample.valid = !err;

    /* The business layer maps values to DP. Publish invalid quality first,
     * retaining the sampling failure when publication also fails. */
    status_set_phase(status, STATUS_PUBLISHING);
    int publish_err = points_publish_snapshot(&sample);
    if (err) {
        return lock_fault(status, STATUS_FAULT_SAMPLE, STATUS_SAMPLING, err);
    }
    if (publish_err) {
        return lock_fault(status, STATUS_FAULT_PUBLISH, STATUS_PUBLISHING, publish_err);
    }

    status_set_phase(status, STATUS_SERVICING_COMMUNICATION);
    protocol_update_status(status);
    err = protocol_error();
    if (err) {
        return lock_fault(status, STATUS_FAULT_PROTOCOL, STATUS_SERVICING_COMMUNICATION, err);
    }

    status_set_phase(status, STATUS_CHECKING_HEALTH);
    err = status_progress(status);
    if (err) {
        return err; /* status_progress already retained the precise source. */
    }
    status_set_phase(status, STATUS_RUN);
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
