#include "device.h"
#include "signal_ids.h"
#include "mvio_signals.h"
#include <platform_callback.h>
#include <errno.h>

/* 配置表按参考工程组织为一个编译单元，保留分文件维护。
 * 数组在此可见，设备关联直接使用指针和 ARRAY_SIZE，无需手工同步条目数。
 * tables 下的 .c 文件不再单独加入编译。
 */
#include "tables/process.c"
#include "tables/collect.c"
#include "tables/transmit.c"

/* 设备身份及其点表关系全部在此声明，设备表和关联的点表均保存在 Flash。
 * 同型号实例直接指向同一数组；通用校验和运行状态初始化由 DP 模块完成。
 */
static const DEV_TABLE_S devices[] = {
    {
        .dev_id = IDU_DEV_LOCAL,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(local_process),
            .pst_table = local_process,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_MB_SLAVE,
            .sig_num = ARRAY_SIZE(transmit_table),
            .pst_table = transmit_table,
        },
    },
    {
        .dev_id = IDU_DEV_REMOTE_IO, .port = 0, .coll_addr = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(mvio_process),
            .pst_table = mvio_process,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_MB_MASTER,
            .sig_num = ARRAY_SIZE(mvio_collect),
            .pst_table = mvio_collect,
        },
    },
#if defined(CONFIG_IDU_AIDC_PROFILE)
    {
        .dev_id = 247,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_local),
            .pst_table = imported_local,
        },
    },
/* BEGIN IMPORTED TABLE: devices */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {
        .dev_id = 11, .coll_addr = 1, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 12, .coll_addr = 2, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 13, .coll_addr = 3, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 14, .coll_addr = 4, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 15, .coll_addr = 5, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 16, .coll_addr = 6, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 17, .coll_addr = 7, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
    {
        .dev_id = 18, .coll_addr = 8, .port = 1,
        .ptl_tbl_cfg[0] = {
            .proto_type = PRO_DATA_PROCESS,
            .sig_num = ARRAY_SIZE(imported_dsp),
            .pst_table = imported_dsp,
        },
        .ptl_tbl_cfg[1] = {
            .proto_type = PRO_UPP_MASTER,
            .sig_num = IMPORTED_MODEL_FIELD_COUNT,
            .pst_table = &imported_models,
        },
    },
/* END IMPORTED TABLE: devices */
#endif
};

/* getter 仅返回配置，不按设备号决定挂表关系，也不修改配置。 */
const DEV_TABLE_S *idu_device_table(void)
{
    return devices;
}

uint16_t idu_device_count(void) { return ARRAY_SIZE(devices); }

/* 参数查询同样从设备配置读取关联，无需维护另一份设备号到型号的分支。 */
const DM_SIG_CFG_S *idu_process_table(uint8_t dev, uint16_t *count)
{
    *count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(devices); ++i) {
        if (devices[i].dev_id != dev) {
            continue;
        }
        const DEV_TBL_CFG_S *group = &devices[i].ptl_tbl_cfg[0];
        if (group->proto_type == PRO_DATA_PROCESS) {
            *count = group->sig_num;
            return group->pst_table;
        }
        break;
    }
    return NULL;
}

/* 参考表保存周期编码；其对应的实际采样周期属于产品配置。 */
static int collect_period(uint8_t code, uint32_t *ms)
{
    switch (code) {
    case 0: *ms = 0; return 0;
    case IDU_COLLECT_SAMPLE: *ms = 500; return 0;
    case IDU_COLLECT_IDENTITY: *ms = 5000; return 0;
    default: return -ENOTSUP;
    }
}

#if defined(CONFIG_IDU_AIDC_PROFILE)
static int model_period(uint8_t code, uint32_t *ms)
{
    ARG_UNUSED(code);
    /* 虚拟模型显式指定周期，不推测旧平台调度单位。 */
    *ms = CONFIG_IDU_AIDC_VIRTUAL_PERIOD_MS;
    return 0;
}
#endif

/* 应用只登记配置回调；通用校验、执行视图及数据池初始化由 dp_init() 完成。
 * 在启动 DP、采集和转发线程之前调用，运行期不得重新注册。
 */
int idu_register_point_tables(void)
{
    int err = dp_register_collect_period(collect_period);
    if (err) { return err; }
#if defined(CONFIG_IDU_AIDC_PROFILE)
    err = dp_register_model_period(model_period);
    if (err) { return err; }
#endif
    pltcb_reg_cfg_get_dev_tbl(idu_device_table);
    pltcb_reg_cfg_get_dev_num(idu_device_count);
    pltcb_reg_para_range_def_cb(idu_point_default);
    pltcb_reg_para_is_load_def_cb(idu_point_restore);
    return 0;
}
