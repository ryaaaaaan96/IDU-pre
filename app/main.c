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
    const char *description = "输入初始化失败";
    enum status_action action = CONFIG_APP_FAULT_INPUT_INIT_ACTION;

    err = acquisition_inputs_init();
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_init();
        description = "升级服务初始化失败";
        action = CONFIG_APP_FAULT_IAP_INIT_ACTION;
    }
#endif
    if (!err) {
        err = protocol_init();
        description = "协议初始化失败";
        action = CONFIG_APP_FAULT_PROTOCOL_INIT_ACTION;
    }
    if (err) {
        if (status_phase_get() != STATUS_FAULT_LOCKED) {
            (void)status_report_fault(STATUS_INIT, err, description, action);
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
        /* 周期超时后直接开始下一轮，不补采错过的历史样本。 */
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
