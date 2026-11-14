#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include "acquisition/acquisition.h"
#include "fan_control/app_fan_control.h"
#include "protocol/inc/communication.h"
#include "protocol/inc/protocol.h"
#include "status/inc/status.h"
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

static int startup(void)
{
    int err = status_init();
    if (err) {
        return err;
    }
    const char *description = "Protocol initialization failed";
    err = communication_init();
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_init();
        description = "Upgrade service initialization failed";
    }
#endif
    if (!err) {
        err = acquisition_init();
        description = "Input initialization failed";
    }

#ifdef CONFIG_FAN
    if (!err) {
        err = app_fan_control_init();
        description = "Fan initialization failed";
    }
#endif

#ifdef CONFIG_BOOTLOADER_MCUBOOT
    /* 必要初始化完成后确认镜像，不再设置运行健康计时窗口。 */
    if (!err) {
        err = boot_write_img_confirmed();
        description = "Image confirmation failed";
    }
#endif

    if (err) {
        watchdog_report_fault(err, description, WATCHDOG_ACTION_HOLD, 0);
        return err;
    }

    k_msleep(10000);

    status_set_phase(STATUS_RUN);
    return 0;
}

int main(void)
{
    int err = startup();
    if (err) {
        status_fault_wait(err);
    }
    return 0;
}
