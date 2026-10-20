#include "acquisition/idu_acquisition.h"
#include <gpio/idu_inputs.h>
#include "process/idu_process.h"
#include "cfg/inc/ports.h"
#include <transmit/server.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_acquisition, LOG_LEVEL_INF);

static int lock_fault(struct idu_status *status,
                      enum idu_status_phase phase, int error)
{
    idu_status_fail(status, phase, error);
    LOG_ERR("Acquisition fault in phase %d: %d; watchdog feeding stopped",
            status->fault_phase, status->last_error);
    return error;
}

int idu_acquisition_start(struct idu_status *status)
{
    if (!status) { return -EINVAL; }
    if (status->phase != IDU_STATUS_UNCONFIGURED) { return -EALREADY; }
    int err = idu_status_start(status);
    if (!err) { err = idu_inputs_init(); }
    if (!err) { err = idu_equipment_init(); }
#ifdef CONFIG_IDU_IAPV3
    if (!err) { err = iap_start(); }
#endif
    if (err) { return lock_fault(status, status->phase, err); }
    idu_status_set_phase(status, IDU_STATUS_WAITING);
    return 0;
}

int idu_acquisition_cycle(struct idu_status *status)
{
    if (!status) { return -EINVAL; }
    if (status->phase != IDU_STATUS_WAITING) { return -EACCES; }

    idu_status_set_phase(status, IDU_STATUS_SAMPLING);
    struct idu_snapshot sample = {0};
    int err = idu_inputs_sample(&sample);
    sample.valid = !err;

    /* The business layer maps values to DP. Publish invalid quality first,
     * retaining the sampling failure when publication also fails. */
    idu_status_set_phase(status, IDU_STATUS_PUBLISHING);
    int publish_err = idu_snapshot_publish(&sample);
    if (err) { return lock_fault(status, IDU_STATUS_SAMPLING, err); }
    if (publish_err) { return lock_fault(status, status->phase, publish_err); }

    idu_status_set_phase(status, IDU_STATUS_CHECKING_HEALTH);
    if (!idu_equipment_healthy(idu_status_timeout_ms())) {
        return lock_fault(status, status->phase, -ETIMEDOUT);
    }
    idu_status_set_phase(status, IDU_STATUS_SERVICING_COMMUNICATION);
    err = tr_server_service(&idu_forwarding, sample.address);
    if (err) { return lock_fault(status, status->phase, err); }

    idu_status_set_phase(status, IDU_STATUS_CHECKING_HEALTH);
    err = idu_status_progress(status);
    if (err) { return lock_fault(status, status->phase, err); }
    idu_status_set_phase(status, IDU_STATUS_WAITING);
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
