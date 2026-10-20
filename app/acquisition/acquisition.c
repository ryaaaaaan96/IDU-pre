#include "acquisition/idu_acquisition.h"
#include "acquisition/inputs.h"
#include "process/idu_process.h"
#include "communication/idu_protocol.h"
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_acquisition, LOG_LEVEL_INF);

static int lock_fault(struct idu_acquisition *acquisition,
                      enum idu_acquisition_state phase, int error)
{
    acquisition->fault_phase = phase;
    acquisition->last_error = error;
    acquisition->state = IDU_ACQUISITION_FAULT_LOCKED;
    idu_watchdog_stop();
    LOG_ERR("Acquisition fault in phase %d: %d; watchdog feeding stopped", phase, error);
    return error;
}

int idu_acquisition_start(struct idu_acquisition *acquisition)
{
    if (!acquisition) { return -EINVAL; }
    if (acquisition->state != IDU_ACQUISITION_UNCONFIGURED) { return -EALREADY; }
    acquisition->state = IDU_ACQUISITION_STARTING;
    int err = idu_watchdog_start();
    if (!err) { err = idu_inputs_init(); }
    if (!err) { err = idu_equipment_init(); }
#ifdef CONFIG_IDU_IAPV3
    if (!err) { err = iap_start(); }
#endif
    if (err) { return lock_fault(acquisition, acquisition->state, err); }
    acquisition->state = IDU_ACQUISITION_WAITING;
    return 0;
}

int idu_acquisition_cycle(struct idu_acquisition *acquisition)
{
    if (!acquisition) { return -EINVAL; }
    if (acquisition->state != IDU_ACQUISITION_WAITING) { return -EACCES; }

    acquisition->state = IDU_ACQUISITION_SAMPLING;
    struct idu_snapshot sample = {0};
    int err = idu_inputs_sample(&sample);
    sample.valid = !err;

    /* The business layer maps values to DP. Publish invalid quality first,
     * retaining the sampling failure when publication also fails. */
    acquisition->state = IDU_ACQUISITION_PUBLISHING;
    int publish_err = idu_snapshot_publish(&sample);
    if (err) { return lock_fault(acquisition, IDU_ACQUISITION_SAMPLING, err); }
    if (publish_err) { return lock_fault(acquisition, acquisition->state, publish_err); }

    acquisition->state = IDU_ACQUISITION_CHECKING_HEALTH;
    if (!idu_equipment_healthy(idu_watchdog_timeout_ms())) {
        return lock_fault(acquisition, acquisition->state, -ETIMEDOUT);
    }
    acquisition->state = IDU_ACQUISITION_SERVICING_COMMUNICATION;
    err = idu_protocol_set_address(sample.address);
    if (!err) { err = idu_indicator_toggle(); }
    if (err) { return lock_fault(acquisition, acquisition->state, err); }

    acquisition->state = IDU_ACQUISITION_CHECKING_HEALTH;
    err = idu_watchdog_progress();
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    if (!err) {
        err = idu_boot_health_checkpoint(&acquisition->boot, k_uptime_get(),
                                         idu_equipment_boot_ready());
    }
#endif
    if (err) { return lock_fault(acquisition, acquisition->state, err); }
    acquisition->state = IDU_ACQUISITION_WAITING;
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
