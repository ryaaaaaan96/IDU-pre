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

int acquisition_cycle(void)
{
    if (status_phase_get() != STATUS_RUN) {
        return -EACCES;
    }

    status_set_phase(STATUS_SAMPLING);
    struct acquisition_sample sample = {0};
    int err = acquisition_inputs_sample(&sample);

    sample.valid = !err;

    /* 业务层负责将数值映射到 DP。先发布无效质量；若发布也失败，
     * 仍保留采样失败作为故障来源。 */
    status_set_phase(STATUS_PUBLISHING);
    int publish_err = points_publish_snapshot(&sample);
    if (err) {
        return status_report_fault(STATUS_RUNTIME, err, "Input sampling failed",
                                   CONFIG_APP_FAULT_SAMPLE_ACTION);
    }
    if (publish_err) {
        return status_report_fault(STATUS_RUNTIME, publish_err, "Sample publication failed",
                                   CONFIG_APP_FAULT_PUBLISH_ACTION);
    }

    status_set_phase(STATUS_SERVICING_COMMUNICATION);
    protocol_update_status();
    err = protocol_error();
    if (err) {
        return status_report_fault(STATUS_RUNTIME, err, "Protocol maintenance failed",
                                   CONFIG_APP_FAULT_PROTOCOL_ACTION);
    }

    status_set_phase(STATUS_CHECKING_HEALTH);
    err = status_progress();
    if (err) {
        return err; /* status_progress 已保留准确的故障来源。 */
    }
    status_set_phase(STATUS_RUN);
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
