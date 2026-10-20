/* 本地样本到测点的映射，以及公共测点和参数命令回调。 */
#include "process/idu_process.h"
#include "process/engineering.h"
#include "cfg/inc/signal_ids.h"
#include <gpio/idu_inputs.h>
#include <dataprocess/params.h>
#include <errno.h>

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
    if (!in || in->para_num != 1 || !in->para_all) {
        return -EINVAL;
    }
    /* DP already loaded the registered default. INIT marks identities ready. */
    return in->type == DP_PARA_INIT ? dp_set_para_value(in->para_all) :
           in->type == DP_PARA_SET ? -EACCES : 0;
}

int32_t idu_initialize_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                             void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all) {
        return -EINVAL;
    }
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
