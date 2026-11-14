/* 产品风扇控制回调；通用 PWM 操作由风扇模块完成。 */
#include "dp_callbacks.h"
#include "fan_control/app_fan_control.h"
#include "protocol/inc/process_table.h"
#include <errno.h>

int points_unavailable(const dp_callback_context_t *context)
{
    return context->events & DP_CALLBACK_EVENT_GET ? -ENODATA : 0;
}

int points_fan_pwm(const dp_callback_context_t *context)
{
    if (context->signal_index < PROCESS_FAN_PWM_1 || context->signal_index > PROCESS_FAN_PWM_8) {
        return -EINVAL;
    }
    uint8_t channel = context->signal_index - PROCESS_FAN_PWM_1;
    if (context->events & DP_CALLBACK_EVENT_SET) {
        return app_fan_control_set_duty(channel, context->value->u16);
    }
    if (context->events & DP_CALLBACK_EVENT_GET) {
        return app_fan_control_get_duty(channel, &context->value->u16);
    }
    return 0;
}

int points_fan_type(const dp_callback_context_t *context)
{
    if ((context->events & DP_CALLBACK_EVENT_SET) && context->value->u16 != 0) {
        return -ENOTSUP;
    }
    return 0;
}
