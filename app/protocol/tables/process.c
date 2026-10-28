#include "process_table.h"
#include "signal_keys.h"
#include <dataprocess/dataprocess.h>
#include <zephyr/sys/util.h>

/* 逐行配置测点；省略的初值和标志为零，未启用范围检查时不填写上下限。 */
static const dp_point_config_t local_process_rows[PROCESS_SIGNAL_COUNT] = {
    [PROCESS_SAMPLE_PERIOD] =
        {
            .signal_key = SIGNAL_SAMPLE_PERIOD,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = CONFIG_IDU_SAMPLE_PERIOD_MS,
                    .minimum.u16 = 100,
                    .maximum.u16 = 1000,
                },
        },
    [PROCESS_LABEL] =
        {
            .signal_key = SIGNAL_LABEL,
            .size = 16,
            .type = VALUE_TYPE_WORDS,
            .data.initial.words = {0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0},
        },
    [PROCESS_SAVED_ADDRESS] =
        {
            .signal_key = SIGNAL_SAVED_ADDRESS,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = 1,
                    .minimum.u16 = 1,
                    .maximum.u16 = 246,
                },
        },
    [PROCESS_DEVICE_PROPERTIES] =
        {
            .signal_key = 2503,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = 3,
                    .minimum.u16 = 0,
                    .maximum.u16 = UINT16_MAX,
                },
        },
    [PROCESS_MANUFACTURER] =
        {
            .signal_key = 2504,
            .size = 20,
            .type = VALUE_TYPE_WORDS,
            .data.initial.words = {0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0},
        },
    [PROCESS_FIRMWARE_VERSION] =
        {
            .signal_key = 2581,
            .size = 30,
            .type = VALUE_TYPE_WORDS,
            .data.initial.words =
                {0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000, 0, 0, 0, 0, 0, 0, 0, 0},
        },
    [PROCESS_HARDWARE_VERSION] =
        {
            .signal_key = 2568,
            .size = 8,
            .type = VALUE_TYPE_WORDS,
            .data.initial.words = {0x3031, 0x4152, 3, 0x0103},
        },
    [PROCESS_DEVICE_TYPE] =
        {
            .signal_key = 8149,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = DEVICE_TYPE,
                    .minimum.u16 = 0,
                    .maximum.u16 = UINT16_MAX,
                },
        },
    [PROCESS_PROTOCOL] =
        {
            .signal_key = SIGNAL_PROTOCOL,
            .size = 4,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U32,
            .data =
                {
                    .initial.u32 = PROTOCOL_VERSION,
                    .minimum.u32 = 0,
                    .maximum.u32 = UINT32_MAX,
                },
        },
    [PROCESS_PRODUCT_CODE] =
        {
            .signal_key = 5486,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = 0x1204,
                    .minimum.u16 = 0,
                    .maximum.u16 = UINT16_MAX,
                },
        },
    [PROCESS_VIRTUAL_MODE] =
        {
            .signal_key = SIGNAL_VIRTUAL_MODE,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = IS_ENABLED(CONFIG_IDU_TEST_FIXTURES),
                    .minimum.u16 = 0,
                    .maximum.u16 = UINT16_MAX,
                },
        },
    [PROCESS_ADDRESS] =
        {
            .signal_key = SIGNAL_ADDRESS,
            .size = 2,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .type = VALUE_TYPE_U16,
            .data =
                {
                    .initial.u16 = 1,
                    .minimum.u16 = 1,
                    .maximum.u16 = 16,
                },
        },
    [PROCESS_DI] =
        {
            .signal_key = SIGNAL_DI,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_1] =
        {
            .signal_key = SIGNAL_CURRENT_1,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_2] =
        {
            .signal_key = SIGNAL_CURRENT_2,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_3] =
        {
            .signal_key = SIGNAL_CURRENT_3,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_4] =
        {
            .signal_key = SIGNAL_CURRENT_4,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_5] =
        {
            .signal_key = SIGNAL_CURRENT_5,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_6] =
        {
            .signal_key = SIGNAL_CURRENT_6,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_7] =
        {
            .signal_key = SIGNAL_CURRENT_7,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
    [PROCESS_CURRENT_8] =
        {
            .signal_key = SIGNAL_CURRENT_8,
            .size = 2,
            .type = VALUE_TYPE_U16,
        },
};

/* 与测点表在同一编译单元，只读表不包含设备 ID 和运行数据；仅供 process_runtime 使用。 */
const dp_table_t protocol_local_table = {
    .points = local_process_rows,
    .point_count = ARRAY_SIZE(local_process_rows),
};
