/* Map board samples to the product point table. */
#include "idu_equipment.h"
#include "cfg/tables/signal_ids.h"
#include "hardware/idu_pre.h"
#include <dataprocess/params.h>
#include <dataprocess/points.h>
#include <errno.h>
#include <zephyr/device.h>

bool idu_equipment_healthy(uint32_t max_idle_ms)
{
    return dp_healthy(max_idle_ms);
}

#ifdef CONFIG_BOOTLOADER_MCUBOOT
bool idu_equipment_boot_ready(void)
{
    const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(external_flash));
    struct dp_param_status status;
    dp_params_get_status(&status);
    return device_is_ready(flash) && (status.flags & DP_PARAM_STORAGE_READY) &&
           !(status.flags & DP_PARAM_PENDING);
}
#endif

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
    return dp_submit_sig_values(values, ARRAY_SIZE(values), K_MSEC(20), NULL);
}
