/* Assemble the IDU point pipeline in application-thread context. */
#include "idu_equip.h"
#include <data_process.h>
#include <platform_callback.h>
#include <dataprocess/process.h>
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_startup, LOG_LEVEL_INF);

/* Buses must exist before parameter recovery; INIT precedes polling. */
int idu_equipment_init(void)
{
    const struct dp_equip_cfg *cfg = idu_equipment_config();
    /* Project configuration is registered before the platform starts. Native
     * IDU tables retain NVS migration hooks and explicit acquisition metadata.
     */
    int err = dp_register_config_provider(idu_equipment_config);
    if (err) {
        return err;
    }
    err = dp_init();
    if (err) {
        return err;
    }
    int iface = idu_acquisition_port_init();
    if (iface < 0) {
        return iface;
    }
    err = co_init(cfg, iface, IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL));
    if (err) { return err; }
#if defined(CONFIG_IDU_AIDC_PROFILE)
    err = idu_aidc_bus_init(cfg);
    if (err) { return err; }
    LOG_WRN("AIDC profile: DSP1..8 use a VIRTUAL UPP model backend");
#endif
#if defined(CONFIG_IDU_AIDC_PROFILE)
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_aidc_settings));
#else
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));
#endif
    err = dp_params_init(cfg, storage_id);
    if (err) {
        LOG_ERR("Parameter storage unavailable (%d); writes disabled", err);
    } else {
        err = dp_params_recover();
        if (err) {
            LOG_WRN("Parameter transaction awaiting readback: %d", err);
        }
    }
    static const struct dp_process_plan process_plan = {.initialize_all = true};
    err = dp_process_start(&process_plan);
    if (!err) {
        err = dp_reference_start();
    }
    return err ? err : co_start();
}
