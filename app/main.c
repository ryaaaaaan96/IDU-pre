#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <zephyr/logging/log.h>
#include "acquisition/acquisition.h"
#include "process/points.h"
#include "protocol/inc/protocol.h"
#include "status/inc/status.h"
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

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

#ifdef CONFIG_BOOTLOADER_MCUBOOT
    /* 必要初始化完成后确认镜像，不再设置运行健康计时窗口。 */
    if (!err) {
        err = boot_write_img_confirmed();
        description = "Image confirmation failed";
    }
#endif

    if (err) {
        return watchdog_report_fault(err, description,
                                    WATCHDOG_ACTION_HOLD, 0);
    }

    k_msleep(10000);

    status_set_phase(STATUS_RUN);
    return 0;
}

/* 主循环定期采样并写入测点。 */
int application_cycle(void)
{
    if (status_phase_get() != STATUS_RUN) {
        return -EACCES;
    }
    int err = acquisition_cycle();
    if (err) {
        return watchdog_report_fault(err, "Input point update failed", WATCHDOG_ACTION_RESET, 200);
    }
    return 0;
}

int main(void)
{
    int err = startup();

    /* 故障上报已关闭业务；主线程只在正常状态执行采样和维护。 */
    while (!err && status_phase_get() == STATUS_RUN) {
        int64_t next_sample = k_uptime_get() + points_sample_period_ms();
        err = application_cycle();
        if (err || status_phase_get() != STATUS_RUN) { break; }
        /* 周期超时后直接开始下一轮，不补采错过的历史样本。 */
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
    /* HOLD 在此留驻；RESET 的独立定时器仍可随时复位。
     * 只查询诊断，不重复上报、不续期任务、不恢复业务。
     */
    while (true) {
        struct watchdog_fault fault;
        if (watchdog_fault_get(&fault) == 0) {
            LOG_ERR("Terminal fault: %s, error %d, action %d",
                    fault.description, fault.error, fault.action);
        } else {
            LOG_ERR("Application stopped: error %d", err);
        }
        k_sleep(K_SECONDS(5));
    }
}
