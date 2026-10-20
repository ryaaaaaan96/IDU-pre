#include "idu_runtime.h"
#include "idu_equipment.h"
#include "communication/idu_protocol.h"
#include "hardware/idu_pre.h"
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_runtime, LOG_LEVEL_INF);

static int stop(struct idu_runtime *runtime, int error)
{
    runtime->state = IDU_RUNTIME_STOPPED;
    idu_health_stop();
    LOG_ERR("Application stopped: %d; watchdog feeding stopped", error);
    return error;
}

int idu_runtime_init(struct idu_runtime *runtime)
{
    if (!runtime) { return -EINVAL; }
    if (runtime->state != IDU_RUNTIME_NEW) { return -EALREADY; }

    int err = idu_health_init();
    if (!err) { err = idu_io_init(); }
    if (!err) { err = idu_analog_init(); }
    if (!err) { err = idu_equipment_init(); }
#ifdef CONFIG_IDU_IAPV3
    if (!err) { err = iap_start(); }
#endif
    if (err) { return stop(runtime, err); }
    runtime->state = IDU_RUNTIME_RUNNING;
    return 0;
}

int idu_runtime_step(struct idu_runtime *runtime)
{
    if (!runtime) { return -EINVAL; }
    if (runtime->state != IDU_RUNTIME_RUNNING) { return -EACCES; }

    struct idu_snapshot sample = {0};
    int err = idu_io_read(&sample.di, &sample.address);
    if (!err) { err = idu_analog_read(sample.raw); }
    sample.valid = !err;

    /* Publish invalid quality before stopping, preserving the acquisition error. */
    int publish_err = idu_snapshot_publish(&sample);
    if (!err) { err = publish_err; }
    if (!err && !idu_equipment_healthy(1500)) { err = -ETIMEDOUT; }
    if (!err) { err = idu_protocol_set_address(sample.address); }
    if (!err) { err = idu_led_toggle(); }
    if (err) { return stop(runtime, err); }

    idu_health_kick();
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    err = idu_boot_health_checkpoint(&runtime->boot, k_uptime_get(),
                                     idu_equipment_boot_ready());
    if (err) { return stop(runtime, err); }
#endif
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
