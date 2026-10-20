#include "device.h"
#include "signal_ids.h"
#include "device_instances.h"
#include "mvio_signals.h"
#include <platform_callback.h>
#include <errno.h>

#define MVIO_DEVICE(dev, bus, unit, north, address_reg) \
    {.dev_id = dev, .port = bus, .coll_addr = unit},

/* 产品设备清单：设备号、端口和从机地址由 IDU 配置决定。
 * 同型号设备关联同一份 Flash 点表，仅这些小型设备描述在启动时装配。
 * 本文件不创建线程、不管理数据池，也不负责三表校验。
 */
static DEV_TABLE_S devices[] = {
    {.dev_id = IDU_DEV_LOCAL},
    IDU_MVIO_INSTANCES(MVIO_DEVICE)
#if defined(CONFIG_IDU_AIDC_PROFILE)
    {.dev_id = 247},
/* BEGIN IMPORTED TABLE: devices */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {.dev_id=11,.coll_addr=1,.port=1},
    {.dev_id=12,.coll_addr=2,.port=1},
    {.dev_id=13,.coll_addr=3,.port=1},
    {.dev_id=14,.coll_addr=4,.port=1},
    {.dev_id=15,.coll_addr=5,.port=1},
    {.dev_id=16,.coll_addr=6,.port=1},
    {.dev_id=17,.coll_addr=7,.port=1},
    {.dev_id=18,.coll_addr=8,.port=1},
/* END IMPORTED TABLE: devices */
#endif
};

/* 设备表 getter：由 DP 在初始化时调用，启动后只读。 */
const DEV_TABLE_S *idu_device_table(void)
{
    static bool assembled;
    if (!assembled) {
        for (size_t i = 0; i < ARRAY_SIZE(devices); ++i) {
            DEV_TABLE_S *dev = &devices[i];
            DEV_TBL_CFG_S *process = &dev->ptl_tbl_cfg[0];
            process->proto_type = PRO_DATA_PROCESS;
            process->pst_table = idu_process_table(dev->dev_id, &process->sig_num);
            DEV_TBL_CFG_S *protocol = &dev->ptl_tbl_cfg[1];
            if (dev->dev_id == IDU_DEV_LOCAL) {
                protocol->proto_type = PRO_MB_SLAVE;
                protocol->pst_table = idu_transmit_table(&protocol->sig_num);
#if defined(CONFIG_IDU_AIDC_PROFILE)
            } else if (dev->dev_id >= IDU_DEV_DSP_FIRST && dev->dev_id <= IDU_DEV_DSP_LAST) {
                protocol->proto_type = PRO_UPP_MASTER;
                protocol->pst_table = idu_model_table(&protocol->sig_num);
#endif
            } else if (dev->dev_id != 247) {
                protocol->proto_type = PRO_MB_MASTER;
                protocol->pst_table = idu_collect_table(&protocol->sig_num);
            }
        }
        assembled = true;
    }
    return devices;
}

uint16_t idu_device_count(void) { return ARRAY_SIZE(devices); }

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
