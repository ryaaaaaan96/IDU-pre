/* 公共测点和参数命令回调；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/params.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

uint16_t points_sample_period_ms(void)
{
    uint8_t bytes[2];
    if (dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, sizeof(bytes), bytes) < 0) {
        return CONFIG_IDU_SAMPLE_PERIOD_MS;
    }
    return bytes[0] | ((uint16_t)bytes[1] << 8);
}

int points_parameter_command(const dp_callback_context_t *context)
{
    if (!context || !context->value || context->device_id != DEVICE_LOCAL) {
        return -EINVAL;
    }
    uint16_t expected;
    int (*action)(uint16_t);
    uint16_t argument = 0;
    switch (context->signal_key) {
    case SIGNAL_RESTORE_LOCAL:
        expected = 0xa55a;
        action = dp_params_restore;
        argument = DEVICE_LOCAL;
        break;
    case SIGNAL_RESTORE_REMOTE:
        expected = 0xa55a;
        action = dp_params_restore;
        argument = DEVICE_REMOTE_IO;
        break;
    default:
        return -ENOENT;
    }
    if (context->value->words[0] != expected) {
        return -EINVAL;
    }
    int err = action(argument);
    if (!err) {
        *context->value = (value_t){0};
    }
    return err;
}

int points_parameter_status(const dp_callback_context_t *context)
{
    if (!context || !context->value || context->device_id != DEVICE_LOCAL ||
        context->signal_key != SIGNAL_PARAMETER_STATUS) {
        return -EINVAL;
    }
    dp_param_status_t status;
    dp_params_get_status(&status);
    context->value->words[0] = status.flags;
    context->value->words[1] = (uint16_t)(int16_t)status.error;
    context->value->words[2] = status.sequence & 0xffff;
    context->value->words[3] = status.sequence >> 16;
    return 0;
}
