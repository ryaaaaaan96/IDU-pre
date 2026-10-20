#include "imported_signals.h"
#include "device.h"
#include "tables.h"
#include "mvio_signals.h"
#include "signal_ids.h"
#include <zephyr/sys/util.h>

const MODBUS_RTU_TABLE_S idu_mvio_collect[] = {
    {.sig_id = IDU_MVIO_DI, .reg_addr = 8152, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 0, .reg_addr = 8163, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 1, .reg_addr = 8164, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 2, .reg_addr = 8165, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 3, .reg_addr = 8166, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 4, .reg_addr = 8167, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 5, .reg_addr = 8168, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 6, .reg_addr = 8169, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_CURRENT_1 + 7, .reg_addr = 8170, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_SAMPLE, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_PROTOCOL, .reg_addr = 8150, .reg_num = 2, .reg_type = 4,
     .rqst_cyl = IDU_COLLECT_IDENTITY, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_MVIO_ADDRESS, .reg_addr = 30459, .reg_num = 1, .reg_type = 3,
     .rqst_cyl = IDU_COLLECT_IDENTITY, .byte_order = BYTE_ORDER_DEFAULT},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    /* FLAG_COLLECT process signals use these as explicit write destinations. */
    {.sig_id = IDU_SIG_REMOTE_SETPOINT, .reg_addr = 40000, .reg_num = 1,
     .reg_type = 3, .flag = FLAG_0x03_W_0x06 | FLAG_0x03_w_0x10},
    {.sig_id = IDU_SIG_REMOTE_OFFSET, .reg_addr = 40001, .reg_num = 2,
     .reg_type = 3, .flag = FLAG_0x03_w_0x10},
#endif
};

BUILD_ASSERT(ARRAY_SIZE(idu_mvio_collect) == IDU_MVIO_COLLECT_COUNT,
             "Update IDU_MVIO_COLLECT_COUNT in tables.h");

#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: fields */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
static const UPP_M_MOD_DATA model_1[] = {
    {.sig_id=AIDC_PARAM_PRODUCT_TYPE,.mod_data_offset=48,.mod_data_len=30,.mod_data_type=DATA_TYPE_STRING,.data_len=30,.data_type=DATA_TYPE_STRING,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
};
static const UPP_M_MOD_DATA model_2[] = {
    {.sig_id=AIDC_PARAM_PRODUCT_CODE,.mod_data_offset=8,.mod_data_len=16,.mod_data_type=DATA_TYPE_STRING,.data_len=16,.data_type=DATA_TYPE_STRING,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
    {.sig_id=AIDC_PARAM_CLIENT_CODE,.mod_data_offset=24,.mod_data_len=20,.mod_data_type=DATA_TYPE_STRING,.data_len=20,.data_type=DATA_TYPE_STRING,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
    {.sig_id=AIDC_PARAM_TYPE,.mod_data_offset=94,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
    {.sig_id=AIDC_PARAM_MATRIL_CODE,.mod_data_offset=96,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
    {.sig_id=AIDC_PARAM_VEHICLE_TYPE,.mod_data_offset=62,.mod_data_len=20,.mod_data_type=DATA_TYPE_STRING,.data_len=20,.data_type=DATA_TYPE_STRING,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=205,.dec_p=1},
};
static const UPP_M_MOD_DATA model_99[] = {
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG1,.mod_data_offset=6,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG2,.mod_data_offset=8,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG3,.mod_data_offset=10,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG4,.mod_data_offset=12,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG1,.mod_data_offset=14,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG2,.mod_data_offset=16,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG3,.mod_data_offset=18,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_MAIN_WRK_MODE,.mod_data_offset=20,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
};
static const UPP_M_MOD_DATA model_100[] = {
    {.sig_id=AIDC_U16_FAN_SPEED1,.mod_data_offset=6,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_FAN_SPEED2,.mod_data_offset=8,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_FAN_SPEED3,.mod_data_offset=10,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_RECT1_VOL,.mod_data_offset=12,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_LINE_VOL_RTO_S,.mod_data_offset=16,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_LINE_VOL_STO_T,.mod_data_offset=20,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_PHASE_VOL_R,.mod_data_offset=24,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_PHASE_VOL_S,.mod_data_offset=28,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_PHASE_VOL_T,.mod_data_offset=32,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_BOOST_BUS_VOL,.mod_data_offset=36,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_OUTPUT_VOL,.mod_data_offset=40,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_BAT_VOL,.mod_data_offset=44,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_F32_OUTPUT_CURR,.mod_data_offset=48,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_INT_FAULT_WORD1,.mod_data_offset=52,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_INT_FAULT_WORD2,.mod_data_offset=54,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_INT_WARN_WORD1,.mod_data_offset=56,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_U16_INT_WARN_WORD2,.mod_data_offset=58,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=1,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
};
static const UPP_M_MOD_DATA model_102[] = {
    {.sig_id=AIDC_ARM_COM_CELL_WORK_CMD,.mod_data_offset=6,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ARM_COMT_BUCK_VOL_CMD,.mod_data_offset=8,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ARM_COM_BOOST_VOL_CMD,.mod_data_offset=12,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ARM_COM_CURR_LMT,.mod_data_offset=16,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ARM_AVERAGE_CURR,.mod_data_offset=20,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ARM_COM_FORCE_BAT_WRK_EN,.mod_data_offset=28,.mod_data_len=2,.mod_data_type=DATA_TYPE_U16,.data_len=2,.data_type=DATA_TYPE_U16,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
};
static const UPP_M_MOD_DATA model_103[] = {
    {.sig_id=AIDC_AC2_BAT_LOW_THRESH,.mod_data_offset=6,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_AC2_BAT_HIGH_THRESH,.mod_data_offset=10,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_GRID_FREQUENCY,.mod_data_offset=14,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
    {.sig_id=AIDC_ALTITUDE_DEFAULT,.mod_data_offset=18,.mod_data_len=4,.mod_data_type=DATA_TYPE_S32,.data_len=4,.data_type=DATA_TYPE_S32,.rw_mode=3,.byte_order=0,.set_pri=0,.scan_pri=0,.rqst_cyl=3,.dec_p=1},
};
/* END IMPORTED TABLE: fields */
/* BEGIN IMPORTED TABLE: collect */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
static const UPP_M_MOD models[] = {
    {.p_data=model_1,.data_num=ARRAY_SIZE(model_1),.mod_id=1},
    {.p_data=model_2,.data_num=ARRAY_SIZE(model_2),.mod_id=2},
    {.p_data=model_99,.data_num=ARRAY_SIZE(model_99),.mod_id=99},
    {.p_data=model_100,.data_num=ARRAY_SIZE(model_100),.mod_id=100},
    {.p_data=model_102,.data_num=ARRAY_SIZE(model_102),.mod_id=102},
    {.p_data=model_103,.data_num=ARRAY_SIZE(model_103),.mod_id=103},
};
const UPP_M_MOD_REG idu_imported_models = {
    .p_model_ary=models,.model_num=ARRAY_SIZE(models),.cfg_mode=0,
};
BUILD_ASSERT(ARRAY_SIZE(model_1) +
             ARRAY_SIZE(model_2) +
             ARRAY_SIZE(model_99) +
             ARRAY_SIZE(model_100) +
             ARRAY_SIZE(model_102) +
             ARRAY_SIZE(model_103) == IDU_IMPORTED_MODEL_FIELD_COUNT,
             "Update IDU_IMPORTED_MODEL_FIELD_COUNT in tables.h");
/* END IMPORTED TABLE: collect */
#endif

const UPP_M_MOD_REG *idu_model_table(uint16_t *count)
{
#if defined(CONFIG_IDU_AIDC_PROFILE)
    *count = IDU_IMPORTED_MODEL_FIELD_COUNT;
    return &idu_imported_models;
#else
    *count = 0;
    return NULL;
#endif
}
