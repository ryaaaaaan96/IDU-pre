/* 公共测点和参数命令回调；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/params.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>

int32_t points_default_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all) {
        return -EINVAL;
    }
    /* DP 已加载登记的默认值；INIT 将标识信息标记为就绪。 */
    return in->type == DP_PARA_INIT ? dp_set_para_value(in->para_all) :
           in->type == DP_PARA_SET ? -EACCES : 0;
}

int32_t points_initialize_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
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

uint16_t points_sample_period_ms(void)
{
    struct dp_point_state state;
    int err = dp_params_read((struct dp_key){DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD}, &state);
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

int32_t points_parameter_command(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                               void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all ||
        !in->para_all->pvalue || in->para_all->sig_size != 2 ||
        in->para_all->dev_id != DEVICE_LOCAL) { return -EINVAL; }
    /* 初始化和读取不能重放命令。是否允许多点写由 DP 延后动作接口校验。 */
    if (in->type == DP_PARA_INIT || in->type == DP_PARA_GET) { return 0; }
    if (in->type != DP_PARA_SET) { return -EINVAL; }
    uint16_t expected, argument = 0;
    int (*action)(uint16_t);
    switch (in->para_all->sig_id) {
    case SIGNAL_RESTORE_LOCAL:
        expected = 0xa55a; action = dp_params_restore; argument = DEVICE_LOCAL; break;
    case SIGNAL_RESTORE_REMOTE:
        expected = 0xa55a; action = dp_params_restore; argument = DEVICE_REMOTE_IO; break;
    case SIGNAL_FLUSH_PARAMETERS:
        expected = 1; action = flush_parameters; break;
    case SIGNAL_RECOVER_PARAMETERS:
        expected = 1; action = recover_parameters; break;
    default: return -ENOENT;
    }
    if (sys_get_le16(in->para_all->pvalue) != expected) { return -EINVAL; }
    return dp_defer_para_action(action, argument);
}

int32_t points_parameter_status(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value)
{
    ARG_UNUSED(out);
    ARG_UNUSED(new_value);
    ARG_UNUSED(old_value);
    if (!in || in->para_num != 1 || !in->para_all ||
        !in->para_all->pvalue || in->para_all->sig_size != 8 ||
        in->para_all->dev_id != DEVICE_LOCAL ||
        in->para_all->sig_id != SIGNAL_PARAMETER_STATUS) { return -EINVAL; }
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
