/* 本地样本到测点的映射，以及公共测点和参数命令回调。 */
#include "process/idu_process.h"
#include "process/engineering.h"
#include "protocol/inc/signal_ids.h"
#include <gpio/idu_inputs.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>

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

static int flush_parameters(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_flush();
}

static int recover_parameters(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_recover();
}

int32_t idu_parameter_command(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                               void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all ||
        !in->para_all->pvalue || in->para_all->sig_size != 2 ||
        in->para_all->dev_id != IDU_DEV_LOCAL) { return -EINVAL; }
    /* 初始化和读取不能重放命令。是否允许多点写由 DP 延后动作接口校验。 */
    if (in->type == DP_PARA_INIT || in->type == DP_PARA_GET) { return 0; }
    if (in->type != DP_PARA_SET) { return -EINVAL; }
    uint16_t expected, argument = 0;
    int (*action)(uint16_t);
    switch (in->para_all->sig_id) {
    case IDU_SIG_RESTORE_LOCAL:
        expected = 0xa55a; action = dp_params_restore; argument = IDU_DEV_LOCAL; break;
    case IDU_SIG_RESTORE_REMOTE:
        expected = 0xa55a; action = dp_params_restore; argument = IDU_DEV_REMOTE_IO; break;
    case IDU_SIG_FLUSH_PARAMETERS:
        expected = 1; action = flush_parameters; break;
    case IDU_SIG_RECOVER_PARAMETERS:
        expected = 1; action = recover_parameters; break;
    default: return -ENOENT;
    }
    if (sys_get_le16(in->para_all->pvalue) != expected) { return -EINVAL; }
    return dp_defer_para_action(action, argument);
}

int32_t idu_parameter_status(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all ||
        !in->para_all->pvalue || in->para_all->sig_size != 8 ||
        in->para_all->dev_id != IDU_DEV_LOCAL ||
        in->para_all->sig_id != IDU_SIG_PARAMETER_STATUS) { return -EINVAL; }
    if (in->type == DP_PARA_INIT) { return 0; }
    if (in->type != DP_PARA_GET) { return -EACCES; }
    struct dp_param_status status;
    dp_params_get_status(&status);
    uint8_t *bytes = in->para_all->pvalue;
    sys_put_le16(status.flags, bytes);
    sys_put_le16((uint16_t)(int16_t)status.error, bytes + 2);
    sys_put_le16(status.sequence & 0xffff, bytes + 4);
    sys_put_le16(status.sequence >> 16, bytes + 6);
    return 0;
}
