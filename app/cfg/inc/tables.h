#ifndef IDU_CFG_TABLES_H
#define IDU_CFG_TABLES_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: counts */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
enum {
    IDU_IMPORTED_LOCAL_PROCESS_COUNT = 191,
    IDU_IMPORTED_DSP_PROCESS_COUNT = 41,
    IDU_IMPORTED_MODEL_FIELD_COUNT = 41,
    IDU_IMPORTED_TRANSMIT_COUNT = 107,
};
/* END IMPORTED TABLE: counts */

extern const DM_SIG_CFG_S idu_imported_local_process[];
extern const DM_SIG_CFG_S idu_imported_dsp_process[];
extern const UPP_M_MOD_REG idu_imported_models;
#endif

/* 供静态设备表使用的编译期数量；表定义处校验实际行数。
 * 手写点表增删条目时同步修改此处；导入部分由工具更新。
 */
enum {
    IDU_LOCAL_PROCESS_COUNT = IS_ENABLED(CONFIG_IDU_AIDC_PROFILE) ? 20 : 21,
    IDU_MVIO_PROCESS_COUNT = 13,
    IDU_MVIO_COLLECT_COUNT = 13,
#if defined(CONFIG_IDU_AIDC_PROFILE)
    IDU_TRANSMIT_COUNT = IDU_IMPORTED_TRANSMIT_COUNT + 1,
#else
    IDU_TRANSMIT_COUNT = 33,
#endif
};

/* 不指定数组长度，让定义处根据初始化项推导，防止漏项被静默补零。 */
extern const DM_SIG_CFG_S idu_local_process[];
extern const DM_SIG_CFG_S idu_mvio_process[];
extern const MODBUS_RTU_TABLE_S idu_mvio_collect[];
extern const MB_TABLE_CELL_T idu_transmit_table[];

#endif
