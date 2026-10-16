#include "idu_equip.h"
#include "idu_pre.h"
#include <idu/process.h>
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif
#include <errno.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_equip, LOG_LEVEL_INF);

int idu_equipment_init(void)
{
    const struct idu_equip_cfg *cfg = idu_equipment_config();
    int err = idu_points_init(cfg);
    if (err) {
        return err;
    }
    int iface = idu_acquisition_port_init();
    if (iface < 0) {
        return iface;
    }
    err = idu_collect_init(cfg, iface, IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL));
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
    err = idu_params_init(cfg, storage_id, iface,
                          IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL));
    if (err) {
        LOG_ERR("Parameter storage unavailable (%d); writes disabled", err);
    } else {
        err = idu_params_recover();
        if (err) {
            LOG_WRN("Parameter transaction awaiting readback: %d", err);
        }
    }
    err = idu_process_init_device(0);
    return err ? err : idu_collect_start();
}

int idu_snapshot_publish(const struct idu_snapshot *snapshot)
{
    if (!snapshot) {
        return -EINVAL;
    }
    struct idu_point_batch batch = {.source = IDU_SOURCE_LOCAL, .count = 10};
    batch.points[0] = (struct idu_point_update){
        .key = {IDU_DEV_LOCAL, IDU_SIG_ADDRESS}, .width = 1,
        .value.words = {snapshot->address}, .error = snapshot->valid ? 0 : -EIO,
    };
    batch.points[1] = (struct idu_point_update){
        .key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .width = 1,
        .value.words = {snapshot->di}, .error = snapshot->valid ? 0 : -EIO,
    };
    for (unsigned i = 0; i < IDU_ANALOG_COUNT; ++i) {
        batch.points[i + 2] = (struct idu_point_update){
            .key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + i}, .width = 1,
            .value.words = {snapshot->raw[i]}, .error = snapshot->valid ? 0 : -EIO,
        };
    }
    return idu_points_publish(&batch, K_MSEC(20));
}
