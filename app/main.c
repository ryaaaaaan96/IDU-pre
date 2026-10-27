#include <watchdog/watchdog.h>
#include "acquisition/acquisition.h"
#include "process/points.h"
#include "protocol/inc/protocol.h"
#include "status/inc/status.h"
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif

int startup(void)
{
    int err = status_init();
    if (err) { return err; }
    const char *description = "Input initialization failed";

    err = acquisition_inputs_init();
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_init();
        description = "Upgrade service initialization failed";
    }
#endif
    if (!err) {
        err = protocol_init();
        description = "Protocol initialization failed";
    }
    if (err) {
        if (status_phase_get() != STATUS_FAULT_LOCKED) {
            (void)watchdog_report_fault(err, description, WATCHDOG_ACTION_HOLD);
        }
        return err;
    }

    status_set_phase(STATUS_RUN);
    return 0;
}

/* 维护通信和健康状态归主循环，采样函数只读硬件并写测点。 */
int application_cycle(void)
{
    if (status_phase_get() != STATUS_RUN) {
        return -EACCES;
    }
    int err = acquisition_cycle();
    if (err) {
        return watchdog_report_fault(err, "Input point update failed", WATCHDOG_ACTION_RESET);
    }
    protocol_update_status();
    err = protocol_error();
    if (err) {
        return watchdog_report_fault(err, "Protocol maintenance failed", WATCHDOG_ACTION_RESET);
    }
    return status_progress();
}

int main(void)
{
    int err = startup();

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + points_sample_period_ms();
        err = application_cycle();
        if (err) {
            return err;
        }
        /* 周期超时后直接开始下一轮，不补采错过的历史样本。 */
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
