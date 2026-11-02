#include "process_table.h"
#include "signal_keys.h"
#include <transmit/transmit.h>

/* 独立测试地址，不沿用历史协议地址或 signal_key；DP 点仅作为测试数据源。 */
static const value_scale_t sample_period_scale = {
    .numerator = 100,
    .denominator = 1,
    .offset = 0,
};

const tr_modbus_point_t protocol_forward_points[] = {
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 0,
        .count = 1,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_SAMPLE_PERIOD,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 2,
        .count = 2,
        .word_order = MB_LOW_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_SAMPLE_PERIOD,
            .external_type = VALUE_TYPE_U32,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 4,
        .count = 2,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_SAMPLE_PERIOD,
            .external_type = VALUE_TYPE_U32,
            .scale = &sample_period_scale,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 8,
        .count = 8,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_LABEL,
            .external_type = VALUE_TYPE_WORDS,
        },
    },
    {
        .area = MB_DATA_AREA_INPUT_REGISTER,
        .address = 0,
        .count = 1,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_DI,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_INPUT_REGISTER,
        .address = 1,
        .count = 1,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_CURRENT_1,
            .external_type = VALUE_TYPE_S16,
        },
    },
    {
        .area = MB_DATA_AREA_DISCRETE_INPUT,
        .address = 0,
        .count = 16,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_DI,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_COIL,
        .address = 0,
        .count = 1,
        .word_order = MB_HIGH_WORD_FIRST,
        .mapping = {
            .device_id = DEVICE_LOCAL,
            .signal_index = PROCESS_VIRTUAL_MODE,
            .external_type = VALUE_TYPE_U16,
        },
    },
};
const size_t protocol_forward_point_count = ARRAY_SIZE(protocol_forward_points);
