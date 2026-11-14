#include "process_table.h"
#include "signal_keys.h"
#include "dp_callbacks/dp_callbacks.h"
#include <zephyr/sys/util.h>
static const dp_point_config_t local_process_rows[PROCESS_SIGNAL_COUNT] = {
    [PROCESS_DEVICE_TYPE] =
        {
            .signal_key = SIGNAL_DEVICE_TYPE,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .data.initial.u16 = DEVICE_TYPE,
        },
    [PROCESS_PROTOCOL] =
        {
            .signal_key = SIGNAL_PROTOCOL,
            .size = 4,
            .type = VALUE_TYPE_U32,
            .data.initial.u32 = PROTOCOL_VERSION,
        },
    [PROCESS_DI] =
        {
            .signal_key = SIGNAL_DI,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_FAN_DET] =
        {
            .signal_key = SIGNAL_FAN_DET,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_COMM_FAULT] =
        {
            .signal_key = SIGNAL_FAN_COMM_FAULT,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_1] =
        {
            .signal_key = SIGNAL_FAN_FAULT_1,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_2] =
        {
            .signal_key = SIGNAL_FAN_FAULT_2,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_3] =
        {
            .signal_key = SIGNAL_FAN_FAULT_3,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_4] =
        {
            .signal_key = SIGNAL_FAN_FAULT_4,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_5] =
        {
            .signal_key = SIGNAL_FAN_FAULT_5,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_6] =
        {
            .signal_key = SIGNAL_FAN_FAULT_6,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_7] =
        {
            .signal_key = SIGNAL_FAN_FAULT_7,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_FAULT_8] =
        {
            .signal_key = SIGNAL_FAN_FAULT_8,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_unavailable,
        },
    [PROCESS_FAN_SPEED_1] =
        {
            .signal_key = SIGNAL_FAN_SPEED_1,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_2] =
        {
            .signal_key = SIGNAL_FAN_SPEED_2,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_3] =
        {
            .signal_key = SIGNAL_FAN_SPEED_3,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_4] =
        {
            .signal_key = SIGNAL_FAN_SPEED_4,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_5] =
        {
            .signal_key = SIGNAL_FAN_SPEED_5,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_6] =
        {
            .signal_key = SIGNAL_FAN_SPEED_6,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_7] =
        {
            .signal_key = SIGNAL_FAN_SPEED_7,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_FAN_SPEED_8] =
        {
            .signal_key = SIGNAL_FAN_SPEED_8,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_feedback_read,
        },
    [PROCESS_TEMPERATURE_1] =
        {
            .signal_key = SIGNAL_TEMPERATURE_1,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_2] =
        {
            .signal_key = SIGNAL_TEMPERATURE_2,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_3] =
        {
            .signal_key = SIGNAL_TEMPERATURE_3,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_4] =
        {
            .signal_key = SIGNAL_TEMPERATURE_4,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_5] =
        {
            .signal_key = SIGNAL_TEMPERATURE_5,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_6] =
        {
            .signal_key = SIGNAL_TEMPERATURE_6,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_7] =
        {
            .signal_key = SIGNAL_TEMPERATURE_7,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_8] =
        {
            .signal_key = SIGNAL_TEMPERATURE_8,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_9] =
        {
            .signal_key = SIGNAL_TEMPERATURE_9,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_10] =
        {
            .signal_key = SIGNAL_TEMPERATURE_10,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_11] =
        {
            .signal_key = SIGNAL_TEMPERATURE_11,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_12] =
        {
            .signal_key = SIGNAL_TEMPERATURE_12,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_13] =
        {
            .signal_key = SIGNAL_TEMPERATURE_13,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_14] =
        {
            .signal_key = SIGNAL_TEMPERATURE_14,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_15] =
        {
            .signal_key = SIGNAL_TEMPERATURE_15,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_16] =
        {
            .signal_key = SIGNAL_TEMPERATURE_16,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_17] =
        {
            .signal_key = SIGNAL_TEMPERATURE_17,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_18] =
        {
            .signal_key = SIGNAL_TEMPERATURE_18,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_19] =
        {
            .signal_key = SIGNAL_TEMPERATURE_19,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_20] =
        {
            .signal_key = SIGNAL_TEMPERATURE_20,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_TEMPERATURE_21] =
        {
            .signal_key = SIGNAL_TEMPERATURE_21,
            .size = 2,
            .type = VALUE_TYPE_S16,
            .access_callback = points_analog_read,
        },
    [PROCESS_FAN_PWM_1] =
        {
            .signal_key = SIGNAL_FAN_PWM_1,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_2] =
        {
            .signal_key = SIGNAL_FAN_PWM_2,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_3] =
        {
            .signal_key = SIGNAL_FAN_PWM_3,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_4] =
        {
            .signal_key = SIGNAL_FAN_PWM_4,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_5] =
        {
            .signal_key = SIGNAL_FAN_PWM_5,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_6] =
        {
            .signal_key = SIGNAL_FAN_PWM_6,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_7] =
        {
            .signal_key = SIGNAL_FAN_PWM_7,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_PWM_8] =
        {
            .signal_key = SIGNAL_FAN_PWM_8,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_pwm,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1000,
        },
    [PROCESS_FAN_TYPE] =
        {
            .signal_key = SIGNAL_FAN_TYPE,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .access_callback = points_fan_type,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data.initial.u16 = 0,
            .data.minimum.u16 = 0,
            .data.maximum.u16 = 1,
        },
    [PROCESS_UNIT_ID] =
        {
            .signal_key = SIGNAL_UNIT_ID,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .flags = DP_POINT_FLAG_CHECK_RANGE | DP_POINT_FLAG_PERSISTENT,
            .data.initial.u16 = 1,
            .data.minimum.u16 = 1,
            .data.maximum.u16 = 246,
        },
};
const dp_table_t protocol_local_table = {
    .points = local_process_rows, .point_count = ARRAY_SIZE(local_process_rows)};
