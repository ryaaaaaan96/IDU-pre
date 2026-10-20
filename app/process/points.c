/* Product point initialization, sample mapping and common point callbacks. */
#include "process/idu_process.h"
#include "process/engineering.h"
#include "cfg/inc/device.h"
#include "cfg/inc/signal_ids.h"
#include <io_input/idu_inputs.h>
#include "communication/ports/idu_acquisition.h"
#include <data_process.h>
#include <platform_callback.h>
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "communication/ports/idu_virtual.h"
#endif
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_points, LOG_LEVEL_INF);

/* Buses must exist before parameter recovery; INIT precedes polling. */
int idu_equipment_init(void)
{
    int err = idu_register_point_tables();
    if (!err) { err = dp_init(); }
    if (err) {
        return err;
    }
    const struct dp_equip_cfg *cfg = dp_config();
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

bool idu_equipment_healthy(uint32_t max_idle_ms)
{
    return dp_healthy(max_idle_ms);
}

#ifdef CONFIG_BOOTLOADER_MCUBOOT
bool idu_equipment_confirm_ready(void)
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
            .value.words = {idu_current_from_raw(snapshot->raw[i])},
            .error = !snapshot->valid ? -EIO : snapshot->raw[i] > 4095 ? -ERANGE : 0,
        };
    }
    return dp_submit_sig_values(values, ARRAY_SIZE(values), K_MSEC(20), NULL);
}

int32_t idu_default_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all) { return -EINVAL; }
    /* DP already loaded the registered default. INIT marks identities ready. */
    return in->type == DP_PARA_INIT ? dp_set_para_value(in->para_all) :
           in->type == DP_PARA_SET ? -EACCES : 0;
}

int32_t idu_initialize_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                             void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all) { return -EINVAL; }
    return in->type == DP_PARA_INIT ? dp_set_para_value(in->para_all) : 0;
}

uint16_t idu_sample_period_ms(void)
{
    struct dp_point_state state;
    int err = dp_params_read((struct dp_key){IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD}, &state);
    return err ? CONFIG_IDU_SAMPLE_PERIOD_MS : state.value.words[0];
}

int idu_process_restore(uint16_t dev)
{
    return dp_params_restore(dev);
}

int idu_process_flush(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_flush();
}

int idu_process_recover(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_recover();
}
