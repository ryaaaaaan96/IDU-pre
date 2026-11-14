#include "app_fan_control.h"
#include "protocol/inc/process_table.h"
#include "protocol/inc/signal_keys.h"
#include <fan/fan.h>
#include <common/value_text.h>
#include <errno.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#define FAN_PWM_CHANNELS 8U
#define FAN_PWM_FREQUENCY_HZ 10000U

static const fan_output_config_t output_configs[] = {
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan0),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan1),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan2),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan3),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan4),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan5),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan6),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
    {.pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan7),
        .frequency_hz = FAN_PWM_FREQUENCY_HZ},
};
static fan_output_t output_instances[FAN_PWM_CHANNELS];
static atomic_t outputs_ready;
static K_MUTEX_DEFINE(pwm_lock);
static uint32_t duty[FAN_PWM_CHANNELS];

int app_fan_control_set_duty(uint8_t channel, uint16_t per_mille)
{
    if (channel >= FAN_PWM_CHANNELS || per_mille > 1000) {
        return -EINVAL;
    }
    if (!atomic_get(&outputs_ready)) {
        return -EAGAIN;
    }
    k_mutex_lock(&pwm_lock, K_FOREVER);
    int err = fan_output_set_per_mille(&output_instances[channel], per_mille, FAN_PWM_FREQUENCY_HZ);
    if (!err) {
        duty[channel] = per_mille;
    }
    k_mutex_unlock(&pwm_lock);
    return err;
}

int app_fan_control_get_duty(uint8_t channel, uint16_t *per_mille)
{
    if (channel >= FAN_PWM_CHANNELS || !per_mille) {
        return -EINVAL;
    }
    if (!atomic_get(&outputs_ready)) {
        return -EAGAIN;
    }
    k_mutex_lock(&pwm_lock, K_FOREVER);
    *per_mille = duty[channel];
    k_mutex_unlock(&pwm_lock);
    return 0;
}

/* 数值边界在解析时检查，不进行窄化截断。 */
static int parse_number(const char *text, uint32_t maximum, uint32_t *value)
{
    int64_t parsed;
    int err = value_text_parse_integer(text, 0, maximum, &parsed);
    if (!err) {
        *value = (uint32_t)parsed;
    }
    return err;
}

int app_fan_control_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(output_instances); ++i) {
        int err = fan_output_init(&output_instances[i], &output_configs[i]);
        if (err) {
            return err;
        }
    }

    atomic_set(&outputs_ready, 1);
    return 0;
}

static int cmd_pwm(const struct shell *shell, size_t argc, char **argv)
{
    uint32_t channel, per_mille;
    ARG_UNUSED(argc);
    if (parse_number(argv[1], FAN_PWM_CHANNELS - 1U, &channel) ||
        parse_number(argv[2], 1000U, &per_mille)) {
        shell_error(shell, "Expected channel 0..7 and duty 0..1000 (0.1%%)");
        return -EINVAL;
    }
    int err = app_fan_control_set_duty(channel, per_mille);
    if (!err) {
        shell_print(shell,
            "PWM channel=%u duty=%u.%u%% frequency=%u Hz",
            channel,
            per_mille / 10U,
            per_mille % 10U,
            FAN_PWM_FREQUENCY_HZ);
    } else {
        shell_error(shell, "PWM control failed (%d)", err);
    }
    return err;
}

/* 诊断与协议使用同一份点值，采样实现不向控制侧暴露快照接口。 */
static int cmd_show(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    for (uint8_t i = 0; i < FAN_PWM_CHANNELS; ++i) {
        value_t value = {0};
        int err = dp_get_value_by_index(&(dp_index_access_t){.device_id = DEVICE_LOCAL,
                                            .signal_index = PROCESS_FAN_SPEED_1 + i,
                                            .value_type = VALUE_TYPE_U16,
                                            .flags = DP_ACCESS_FLAG_GET_CALLBACK},
            &value);
        if (err) {
            shell_print(shell, "Fan %u: feedback not available (%d)", i + 1U, err);
        } else {
            shell_print(shell, "Fan %u: feedback=%u (0.1%%)", i + 1U, value.u16);
        }
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(fan_commands,
    SHELL_CMD_ARG(
        pwm, NULL, "Set <channel 0..7> <duty 0..1000 (0.1%)>; fixed 10 kHz.", cmd_pwm, 3, 0),
    SHELL_CMD_ARG(show, NULL, "Show all eight feedback points.", cmd_show, 1, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(fan, &fan_commands, "Fan PWM control and feedback selection.", NULL);
