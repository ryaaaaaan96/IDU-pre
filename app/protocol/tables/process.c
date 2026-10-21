#include "protocol.h"
#include "../tables.h"
#include "signal_ids.h"
#include "process/idu_process.h"
#include <errno.h>
#include <zephyr/sys/util.h>

static const DM_SIG_CFG_S idu_local_process[] = {
    {.sig_id = IDU_SIG_SAMPLE_PERIOD, .sig_size = 2, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_LABEL, .sig_size = 16, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_SAVED_ADDRESS, .sig_size = 2, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = 2503, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = idu_default_point},
    {.sig_id = 2504, .sig_size = 20, .sig_flag = 0,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = idu_default_point},
    {.sig_id = 2581, .sig_size = 30, .sig_flag = 0,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = idu_default_point},
    {.sig_id = 2568, .sig_size = 8, .sig_flag = 0,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = idu_default_point},
    {.sig_id = 8149, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = idu_default_point},
    {.sig_id = IDU_SIG_PROTOCOL, .sig_size = 4, .sig_flag = 0,
     .data_type = DATA_TYPE_U32, .pf_sig_proc = idu_default_point},
    {.sig_id = 5486, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = idu_default_point},
    {.sig_id = IDU_SIG_VIRTUAL_MODE, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = idu_default_point},
    {.sig_id = IDU_SIG_ADDRESS, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_DI, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 0, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 1, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 2, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 3, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 4, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 5, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 6, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 7, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
};

static const DM_SIG_CFG_S idu_mvio_process[] = {
    {.sig_id = IDU_SIG_DI, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 0, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_CURRENT_1 + 1, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_PROTOCOL, .sig_size = 4, .sig_flag = 0,
     .data_type = DATA_TYPE_U32, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_ADDRESS, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_REMOTE_SETPOINT, .sig_size = 2,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_REMOTE_OFFSET, .sig_size = 4,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT,
     .data_type = DATA_TYPE_S32, .pf_sig_proc = NULL},
};

/* 默认值、上下限和恢复策略集中在一张 Flash 数组中。
 * country 未指定时为通用配置；后续国家配置只需增加有差异的行。
 * DM_SIG_CFG_S 和 DM_SIG_DATA_S 保持参考接口定义，不扩展模块结构。
 */
struct point_parameter {
    uint16_t country;
    uint16_t dev_id, sig_id;
    DM_SIG_DATA_S data;
    bool restore;
};

#define U16(dev, sig, value, low, high) \
    {.dev_id = dev, .sig_id = sig, .data = {.def_value.u16 = value, \
     .min_value.u16 = low, .max_value.u16 = high}, .restore = true}
#define U32(dev, sig, value) \
    {.dev_id = dev, .sig_id = sig, .data = {.def_value.u32 = value, \
     .max_value.u32 = UINT32_MAX}, .restore = true}
#define WORDS(dev, sig, ...) \
    {.dev_id = dev, .sig_id = sig, \
     .data.def_value.pst = (void *)(const uint16_t[]){__VA_ARGS__}, .restore = true}

static const uint8_t zeros[144];
static const struct point_parameter parameters[] = {
    U16(IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD, CONFIG_IDU_SAMPLE_PERIOD_MS, 100, 1000),
    WORDS(IDU_DEV_LOCAL, IDU_SIG_LABEL, 0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0),
    U16(IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS, 1, 1, 246),
    U16(IDU_DEV_LOCAL, 2503, 3, 0, UINT16_MAX),
    WORDS(IDU_DEV_LOCAL, 2504, 0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0),
    WORDS(IDU_DEV_LOCAL, 2581, 0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000,
          0, 0, 0, 0, 0, 0, 0, 0),
    WORDS(IDU_DEV_LOCAL, 2568, 0x3031, 0x4152, 3, 0x0103),
    U16(IDU_DEV_LOCAL, 8149, IDU_DEVICE_TYPE, 0, UINT16_MAX),
    U32(IDU_DEV_LOCAL, IDU_SIG_PROTOCOL, IDU_PROTOCOL_VERSION),
    U16(IDU_DEV_LOCAL, 5486, 0x1204, 0, UINT16_MAX),
    U16(IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE, IS_ENABLED(CONFIG_IDU_TEST_FIXTURES), 0, UINT16_MAX),
    U16(IDU_DEV_LOCAL, IDU_SIG_ADDRESS, 1, 1, 16),
    U16(IDU_DEV_REMOTE_IO, IDU_SIG_ADDRESS, 1, 1, 247),
    U16(IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT, 100, 0, 1000),
    {.dev_id = IDU_DEV_REMOTE_IO, .sig_id = IDU_SIG_REMOTE_OFFSET,
     .data = {.min_value.s32 = -100000, .max_value.s32 = 100000}, .restore = true},
};

/* 共用处理表的设备共用型号参数，以配置中第一个实例的设备号为参数键。 */
static uint16_t default_device(uint16_t dev)
{
    uint16_t count;
    const DM_SIG_CFG_S *rows = idu_process_table((uint8_t)dev, &count);
    const DEV_TABLE_S *devices = idu_device_table();
    if (!rows) {
        return dev;
    }
    for (size_t i = 0; i < idu_device_count(); ++i) {
        const DEV_TBL_CFG_S *group = &devices[i].ptl_tbl_cfg[0];
        if (group->proto_type == PRO_DATA_PROCESS && group->pst_table == rows) {
            return devices[i].dev_id;
        }
    }
    return dev;
}

/* 国家行整体覆盖同一测点的通用记录，未配置的测点复用通用记录。
 * 必须先确认国家已配置，不能把未知国家静默解释为通用配置。
 */
static int find_parameter(uint16_t country, uint16_t dev, uint16_t sig,
                          const struct point_parameter **result)
{
    const struct point_parameter *common = NULL;
    bool supported = country == IDU_PARAMETER_COUNTRY_COMMON;

    dev = default_device(dev);
    for (size_t i = 0; i < ARRAY_SIZE(parameters); ++i) {
        const struct point_parameter *entry = &parameters[i];
        if (entry->country == country) {
            supported = true;
        }
        if (entry->dev_id != dev || entry->sig_id != sig) {
            continue;
        }
        if (entry->country == country) {
            *result = entry;
            return 0;
        }
        if (entry->country == IDU_PARAMETER_COUNTRY_COMMON) {
            common = entry;
        }
    }
    if (!supported) {
        return -ENOTSUP;
    }
    *result = common;
    return 0;
}

int idu_point_parameters_get(uint16_t country, uint16_t dev, uint16_t sig,
                             DM_SIG_DATA_S *value, bool *restore)
{
    if (!value || !restore) {
        return -EINVAL;
    }
    /* 参考设备表使用 uint8_t ID，先检查再转换，避免读到截断后的设备。 */
    if (dev > UINT8_MAX) {
        return -ENOENT;
    }
    const struct point_parameter *entry;
    int err = find_parameter(country, dev, sig, &entry);
    if (err) {
        return err;
    }
    uint16_t count;
    const DM_SIG_CFG_S *rows = idu_process_table((uint8_t)dev, &count);
    for (size_t i = 0; i < count; ++i) {
        if (rows[i].sig_id != sig) {
            continue;
        }
        if (entry) {
            *value = entry->data;
            *restore = entry->restore;
        } else {
            /* 保持参考约定：无初值行时补零，范围全零表示使用类型范围。 */
            *value = (DM_SIG_DATA_S){0};
            if (rows[i].sig_size > 4) {
                value->def_value.pst = (void *)zeros;
            }
            *restore = true;
        }
        return 0;
    }
    return -ENOENT;
}

int idu_point_default(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *value, bool *recover)
{
    if (!recover) {
        return -EINVAL;
    }
    bool restore;
    int err = idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON, dev, sig,
                                       value, &restore);
    if (!err) {
        /* 国家参数查询只提供配置，不强制覆盖已有 NVS 快照。 */
        *recover = false;
    }
    return err;
}

bool idu_point_restore(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value)
{
    ARG_UNUSED(size);
    ARG_UNUSED(value);
    DM_SIG_DATA_S data;
    bool restore;
    return idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON, dev, sig,
                                    &data, &restore) == 0 && restore;
}

static const STRUCT_SECTION_ITERABLE(dp_registered_table, local_process_table) = {
    .id = IDU_TABLE_LOCAL_PROCESS, .type = PRO_DATA_PROCESS,
    .rows = idu_local_process, .count = ARRAY_SIZE(idu_local_process),
};

static const STRUCT_SECTION_ITERABLE(dp_registered_table, remote_process_table) = {
    .id = IDU_TABLE_REMOTE_PROCESS, .type = PRO_DATA_PROCESS,
    .rows = idu_mvio_process, .count = ARRAY_SIZE(idu_mvio_process),
};
