#include "idu_pre.h"
#include "idu_equip.h"
#include "idu_boot.h"
#ifdef CONFIG_IDU_IAPV3
#include "idu_iap.h"
#endif
#include <gd32_gpio.h>
#include <gd32_rcu.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_pre, LOG_LEVEL_INF);

int main(void)
{
    /* PA15 is the RS485 DE signal: release JTAG while retaining SWD. */
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

    int err = idu_health_init();
    if (err) {
        LOG_ERR("External watchdog GPIO init failed: %d", err);
        return err;
    }
    err = idu_io_init();
    if (!err) {
        err = idu_analog_init();
    }
    if (!err) {
        err = idu_equipment_init();
    }
    if (err) {
        LOG_ERR("Acquisition init failed: %d", err);
        idu_health_stop();
        return err;
    }

    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    LOG_INF("External flash: %s (legacy contents preserved)",
             device_is_ready(flash) ? "ready" : "not ready");
#ifdef CONFIG_IDU_IAPV3
    err = idu_iap_init();
    if (err) {
        LOG_ERR("IAP initialization failed: %d", err);
        idu_health_stop();
        return err;
    }
#endif

    struct idu_snapshot sample = {0};
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct idu_boot_health boot_health = {0};
#endif
    while (true) {
        int64_t next_sample = k_uptime_get() + idu_sample_period_ms();
        err = idu_io_read(&sample.di, &sample.address);
        if (!err) {
            err = idu_analog_read(sample.raw);
        }
        sample.valid = err == 0;
        /* Engineering conversion now belongs to the process point table. */
        int publish_err = idu_snapshot_publish(&sample);
        if (!err) {
            err = publish_err;
        }
        if (!err && !idu_points_healthy(1500)) {
            err = -ETIMEDOUT;
        }
        if (err) {
            LOG_ERR("Acquisition failed: %d; watchdog feeding stopped", err);
            idu_health_stop();
            return err;
        }
        err = idu_protocol_set_address(sample.address);
        if (err) {
            LOG_ERR("Modbus init failed: %d", err);
            idu_health_stop();
            return err;
        }
        err = idu_led_toggle();
        if (err) {
            idu_health_stop();
            return err;
        }
        idu_health_kick();
#ifdef CONFIG_BOOTLOADER_MCUBOOT
        struct idu_param_status parameter_status;
        idu_params_get_status(&parameter_status);
        bool ready = device_is_ready(flash) &&
                     (parameter_status.flags & IDU_PARAM_STORAGE_READY) &&
                     !(parameter_status.flags & IDU_PARAM_PENDING);
        err = idu_boot_health_checkpoint(&boot_health, k_uptime_get(), ready);
        if (err) {
            idu_health_stop();
            return err;
        }
#endif
        LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) {
            k_sleep(K_MSEC(delay));
        }
    }
}
