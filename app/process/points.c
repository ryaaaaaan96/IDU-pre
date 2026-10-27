/* 公共测点和参数命令回调；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/params.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

uint16_t points_sample_period_ms(void)
{
    struct dp_point_state state;

    if (dp_params_read((struct dp_key){DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD}, &state)) {
        return CONFIG_IDU_SAMPLE_PERIOD_MS;
    }
    return state.value.words[0];
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

int points_parameter_command(struct dp_key key, const struct dp_value *value)
{
    if (!value || key.dev_id != DEVICE_LOCAL) { return -EINVAL; }
    uint16_t expected;
    int (*action)(uint16_t);
    uint16_t argument = 0;
    switch (key.sig_id) {
    case SIGNAL_RESTORE_LOCAL:
        expected = 0xa55a; action = dp_params_restore; argument = DEVICE_LOCAL; break;
    case SIGNAL_RESTORE_REMOTE:
        expected = 0xa55a; action = dp_params_restore; argument = DEVICE_REMOTE_IO; break;
    case SIGNAL_FLUSH_PARAMETERS:
        expected = 1; action = flush_parameters; break;
    case SIGNAL_RECOVER_PARAMETERS:
        expected = 1; action = recover_parameters; break;
    default:
        return -ENOENT;
    }
    return value->words[0] == expected ? action(argument) : -EINVAL;
}

int points_parameter_status(struct dp_key key, struct dp_point_state *state)
{
    if (!state || key.dev_id != DEVICE_LOCAL ||
        key.sig_id != SIGNAL_PARAMETER_STATUS) { return -EINVAL; }
    struct dp_param_status status;
    dp_params_get_status(&status);
    state->value.words[0] = status.flags;
    state->value.words[1] = (uint16_t)(int16_t)status.error;
    state->value.words[2] = status.sequence & 0xffff;
    state->value.words[3] = status.sequence >> 16;
    state->quality = DP_QUALITY_GOOD;
    state->error = 0;
    state->sampled_at = k_uptime_get();
    return 0;
}
