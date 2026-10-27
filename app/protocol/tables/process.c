#include "protocol.h"
#include "signal_ids.h"
#include "process/points.h"
#include <dataprocess/model.h>
#include <zephyr/sys/util.h>

/* 每行显式配置全部字段；默认值来自 DP，读回调通过 FLAG_READ_CALLBACK 启用。 */
static const DM_SIG_CFG_S local_process_rows[] = {
    {
        .sig_id = SIGNAL_RESTORE_LOCAL,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .handler = points_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_RESTORE_REMOTE,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .handler = points_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_FLUSH_PARAMETERS,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .handler = points_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_RECOVER_PARAMETERS,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .handler = points_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_PARAMETER_STATUS,
        .sig_size = 8,
        .sig_flag = FLAG_READ_CALLBACK,
        .data_type = DATA_TYPE_U16ARRAY,
        .read = points_parameter_status,
        .data = {
            .def_value.pst = DP_POINT_DEFAULT_ARRAY,
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = SIGNAL_SAMPLE_PERIOD,
        .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = CONFIG_IDU_SAMPLE_PERIOD_MS,
            .min_value.u16 = 100,
            .max_value.u16 = 1000,
        },
    },
    {
        .sig_id = SIGNAL_LABEL,
        .sig_size = 16,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
        .data_type = DATA_TYPE_U16ARRAY,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = SIGNAL_SAVED_ADDRESS,
        .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = 1,
            .min_value.u16 = 1,
            .max_value.u16 = 246,
        },
    },
    {
        .sig_id = 2503,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = 3,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = 2504,
        .sig_size = 20,
        .sig_flag = FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16ARRAY,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 2581,
        .sig_size = 30,
        .sig_flag = FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16ARRAY,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000, 0, 0, 0, 0, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 2568,
        .sig_size = 8,
        .sig_flag = FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16ARRAY,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x3031, 0x4152, 3, 0x0103},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 8149,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DEVICE_TYPE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = SIGNAL_PROTOCOL,
        .sig_size = 4,
        .sig_flag = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .data_type = DATA_TYPE_U32,
        .data = {
            .def_value.u32 = PROTOCOL_VERSION,
            .min_value.u32 = DP_POINT_DEFAULT_VALUE,
            .max_value.u32 = UINT32_MAX,
        },
    },
    {
        .sig_id = 5486,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = 0x1204,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = SIGNAL_VIRTUAL_MODE,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK | FLAG_CONSTANT,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = IS_ENABLED(CONFIG_IDU_TEST_FIXTURES),
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = SIGNAL_ADDRESS,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = 1,
            .min_value.u16 = 1,
            .max_value.u16 = 16,
        },
    },
    {
        .sig_id = SIGNAL_DI,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_1,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_2,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_3,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_4,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_5,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_6,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_7,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = SIGNAL_CURRENT_8,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
};

/* 与测点表在同一编译单元，指针及数量均为编译期常量；仅供 process_runtime 使用。 */
const struct dp_model_cfg protocol_local_model = {
    .device_id = DEVICE_LOCAL,
    .points = local_process_rows,
    .point_count = ARRAY_SIZE(local_process_rows),
};
