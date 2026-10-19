#include "idu_equip.h"
#include "idu_pre.h"
#include <dataprocess/process.h>
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif
#include <errno.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_equip, LOG_LEVEL_INF);

int idu_equipment_init(void)
{
    const struct dp_equip_cfg *cfg = idu_equipment_config();
    int err = dp_init(cfg);
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
    return err ? err : co_start();
}

int idu_snapshot_publish(const struct idu_snapshot *snapshot)
{
    if (!snapshot) {
        return -EINVAL;
    }
    /* Product mapping stays here; DP owns batching and sample processing. */
    struct dp_point_update values[2 + IDU_ANALOG_COUNT] = {0};
    values[0] = (struct dp_point_update){
        .key = {IDU_DEV_LOCAL, IDU_SIG_ADDRESS}, .width = 1,
        .value.words = {snapshot->address}, .error = snapshot->valid ? 0 : -EIO,
    };
    values[1] = (struct dp_point_update){
        .key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .width = 1,
        .value.words = {snapshot->di}, .error = snapshot->valid ? 0 : -EIO,
    };
    for (unsigned i = 0; i < IDU_ANALOG_COUNT; ++i) {
        values[i + 2] = (struct dp_point_update){
            .key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + i}, .width = 1,
            .value.words = {snapshot->raw[i]}, .error = snapshot->valid ? 0 : -EIO,
        };
    }
    return dp_set_sig_values(values, ARRAY_SIZE(values), K_MSEC(20), NULL);
}
