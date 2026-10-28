#include "protocol.h"
#include "signal_ids.h"
#include "process/points.h"
#include <dataprocess/points.h>
#include <zephyr/sys/util.h>

/* 每行显式配置全部字段；默认值来自 DP，读回调通过 FLAG_READ_CALLBACK 启用。 */
static const dp_point_def_t local_process_rows[] = {
    {
        .signal_id = SIGNAL_RESTORE_LOCAL,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .write = points_parameter_command,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_RESTORE_REMOTE,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .write = points_parameter_command,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_PARAMETER_STATUS,
        .size = 8,
        .flags = FLAG_READ_CALLBACK,
        .type = DP_TYPE_WORDS,
        .read = points_parameter_status,
        .data = {
            .initial.array = DP_POINT_DEFAULT_ARRAY,
            .minimum.array = DP_POINT_DEFAULT_ARRAY,
            .maximum.array = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .signal_id = SIGNAL_SAMPLE_PERIOD,
        .size = 2,
        .flags = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = CONFIG_IDU_SAMPLE_PERIOD_MS,
            .minimum.u16 = 100,
            .maximum.u16 = 1000,
        },
    },
    {
        .signal_id = SIGNAL_LABEL,
        .size = 16,
        .flags = FLAG_PERSISTENCE,
        .type = DP_TYPE_WORDS,
        .data = {
            .initial.array = (const uint16_t[]){0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0},
            .minimum.array = DP_POINT_DEFAULT_ARRAY,
            .maximum.array = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .signal_id = SIGNAL_SAVED_ADDRESS,
        .size = 2,
        .flags = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = 1,
            .minimum.u16 = 1,
            .maximum.u16 = 246,
        },
    },
    {
        .signal_id = 2503,
        .size = 2,
        .flags = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = 3,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = UINT16_MAX,
        },
    },
    {
        .signal_id = 2504,
        .size = 20,
        .flags = FLAG_CONSTANT,
        .type = DP_TYPE_WORDS,
        .data = {
            .initial.array = (const uint16_t[]){0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0},
            .minimum.array = DP_POINT_DEFAULT_ARRAY,
            .maximum.array = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .signal_id = 2581,
        .size = 30,
        .flags = FLAG_CONSTANT,
        .type = DP_TYPE_WORDS,
        .data = {
            .initial.array = (const uint16_t[]){0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000, 0, 0, 0, 0, 0, 0, 0, 0},
            .minimum.array = DP_POINT_DEFAULT_ARRAY,
            .maximum.array = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .signal_id = 2568,
        .size = 8,
        .flags = FLAG_CONSTANT,
        .type = DP_TYPE_WORDS,
        .data = {
            .initial.array = (const uint16_t[]){0x3031, 0x4152, 3, 0x0103},
            .minimum.array = DP_POINT_DEFAULT_ARRAY,
            .maximum.array = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .signal_id = 8149,
        .size = 2,
        .flags = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DEVICE_TYPE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = UINT16_MAX,
        },
    },
    {
        .signal_id = SIGNAL_PROTOCOL,
        .size = 4,
        .flags = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .type = DP_TYPE_U32,
        .data = {
            .initial.u32 = PROTOCOL_VERSION,
            .minimum.u32 = DP_POINT_DEFAULT_VALUE,
            .maximum.u32 = UINT32_MAX,
        },
    },
    {
        .signal_id = 5486,
        .size = 2,
        .flags = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = 0x1204,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = UINT16_MAX,
        },
    },
    {
        .signal_id = SIGNAL_VIRTUAL_MODE,
        .size = 2,
        .flags = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = IS_ENABLED(CONFIG_IDU_TEST_FIXTURES),
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = UINT16_MAX,
        },
    },
    {
        .signal_id = SIGNAL_ADDRESS,
        .size = 2,
        .flags = FLAG_RANGE_CHECK,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = 1,
            .minimum.u16 = 1,
            .maximum.u16 = 16,
        },
    },
    {
        .signal_id = SIGNAL_DI,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_1,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_2,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_3,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_4,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_5,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_6,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_7,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .signal_id = SIGNAL_CURRENT_8,
        .size = 2,
        .flags = DP_POINT_DEFAULT_FLAGS,
        .type = DP_TYPE_U16,
        .data = {
            .initial.u16 = DP_POINT_DEFAULT_VALUE,
            .minimum.u16 = DP_POINT_DEFAULT_VALUE,
            .maximum.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
};

/* 与测点表在同一编译单元，只读表不包含设备 ID 和运行数据；仅供 process_runtime 使用。 */
const dp_table_t protocol_local_table = {
    .points = local_process_rows,
    .point_count = ARRAY_SIZE(local_process_rows),
};
