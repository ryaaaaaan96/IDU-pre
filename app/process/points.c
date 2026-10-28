/* 公共测点和参数命令回调；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_ids.h"
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

int points_parameter_command(uint16_t device_id, uint16_t signal_id, const value_t *value)
{
    if (!value || device_id != DEVICE_LOCAL) {
        return -EINVAL;
    }
    uint16_t expected;
    int (*action)(uint16_t);
    uint16_t argument = 0;
    switch (signal_id) {
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
    return value->words[0] == expected ? action(argument) : -EINVAL;
}

int points_parameter_status(uint16_t device_id, uint16_t signal_id, value_t *value)
{
    if (!value || device_id != DEVICE_LOCAL || signal_id != SIGNAL_PARAMETER_STATUS) {
        return -EINVAL;
    }
    dp_param_status_t status;
    dp_params_get_status(&status);
    value->words[0] = status.flags;
    value->words[1] = (uint16_t)(int16_t)status.error;
    value->words[2] = status.sequence & 0xffff;
    value->words[3] = status.sequence >> 16;
    return 0;
}
