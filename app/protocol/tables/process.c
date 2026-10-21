#include "protocol.h"
#include "../tables.h"
#include "signal_ids.h"
#include "process/idu_process.h"
#include <zephyr/sys/util.h>

/* 每行显式配置全部字段；默认值来自 DP，读回调通过 FLAG_READ_CALLBACK 启用。 */
static const DM_SIG_CFG_S idu_local_process[] = {
    {
        .sig_id = IDU_SIG_RESTORE_LOCAL,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_RESTORE_REMOTE,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_FLUSH_PARAMETERS,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_RECOVER_PARAMETERS,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_parameter_command,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_PARAMETER_STATUS,
        .sig_size = 8,
        .sig_flag = FLAG_READ_CALLBACK,
        .data_type = DATA_TYPE_U16ARRAY,
        .pf_sig_proc = idu_parameter_status,
        .data = {
            .def_value.pst = DP_POINT_DEFAULT_ARRAY,
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = IDU_SIG_SAMPLE_PERIOD,
        .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = CONFIG_IDU_SAMPLE_PERIOD_MS,
            .min_value.u16 = 100,
            .max_value.u16 = 1000,
        },
    },
    {
        .sig_id = IDU_SIG_LABEL,
        .sig_size = 16,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
        .data_type = DATA_TYPE_U16ARRAY,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = IDU_SIG_SAVED_ADDRESS,
        .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = 1,
            .min_value.u16 = 1,
            .max_value.u16 = 246,
        },
    },
    {
        .sig_id = 2503,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.u16 = 3,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = 2504,
        .sig_size = 20,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16ARRAY,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 2581,
        .sig_size = 30,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16ARRAY,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000, 0, 0, 0, 0, 0, 0, 0, 0},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 2568,
        .sig_size = 8,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16ARRAY,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.pst = (void *)(const uint16_t[]){0x3031, 0x4152, 3, 0x0103},
            .min_value.pst = DP_POINT_DEFAULT_ARRAY,
            .max_value.pst = DP_POINT_DEFAULT_ARRAY,
        },
    },
    {
        .sig_id = 8149,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.u16 = IDU_DEVICE_TYPE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = IDU_SIG_PROTOCOL,
        .sig_size = 4,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U32,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.u32 = IDU_PROTOCOL_VERSION,
            .min_value.u32 = DP_POINT_DEFAULT_VALUE,
            .max_value.u32 = UINT32_MAX,
        },
    },
    {
        .sig_id = 5486,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.u16 = 0x1204,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = IDU_SIG_VIRTUAL_MODE,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = idu_default_point,
        .data = {
            .def_value.u16 = IS_ENABLED(CONFIG_IDU_TEST_FIXTURES),
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = UINT16_MAX,
        },
    },
    {
        .sig_id = IDU_SIG_ADDRESS,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = 1,
            .min_value.u16 = 1,
            .max_value.u16 = 16,
        },
    },
    {
        .sig_id = IDU_SIG_DI,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 0,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 1,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 2,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 3,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 4,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 5,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 6,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 7,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
};

static const DM_SIG_CFG_S idu_mvio_process[] = {
    {
        .sig_id = IDU_SIG_DI,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 0,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_CURRENT_1 + 1,
        .sig_size = 2,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = DP_POINT_DEFAULT_VALUE,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_PROTOCOL,
        .sig_size = 4,
        .sig_flag = DP_POINT_DEFAULT_FLAGS,
        .data_type = DATA_TYPE_U32,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u32 = DP_POINT_DEFAULT_VALUE,
            .min_value.u32 = DP_POINT_DEFAULT_VALUE,
            .max_value.u32 = DP_POINT_DEFAULT_VALUE,
        },
    },
    {
        .sig_id = IDU_SIG_ADDRESS,
        .sig_size = 2,
        .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = 1,
            .min_value.u16 = 1,
            .max_value.u16 = 247,
        },
    },
    {
        .sig_id = IDU_SIG_REMOTE_SETPOINT,
        .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.u16 = 100,
            .min_value.u16 = DP_POINT_DEFAULT_VALUE,
            .max_value.u16 = 1000,
        },
    },
    {
        .sig_id = IDU_SIG_REMOTE_OFFSET,
        .sig_size = 4,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_S32,
        .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
        .data = {
            .def_value.s32 = DP_POINT_DEFAULT_VALUE,
            .min_value.s32 = -100000,
            .max_value.s32 = 100000,
        },
    },
};

static const STRUCT_SECTION_ITERABLE(dp_registered_table, local_process_table) = {
    .id = IDU_TABLE_LOCAL_PROCESS, .type = PRO_DATA_PROCESS,
    .rows = idu_local_process, .count = ARRAY_SIZE(idu_local_process),
};

static const STRUCT_SECTION_ITERABLE(dp_registered_table, remote_process_table) = {
    .id = IDU_TABLE_REMOTE_PROCESS, .type = PRO_DATA_PROCESS,
    .rows = idu_mvio_process, .count = ARRAY_SIZE(idu_mvio_process),
};
