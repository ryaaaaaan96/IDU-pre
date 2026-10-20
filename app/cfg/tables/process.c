#include "imported_signals.h"
#include "device.h"
#include "tables.h"
#include "signal_ids.h"
#include "mvio_signals.h"
#include "process/idu_process.h"
#include <errno.h>
#include <zephyr/sys/util.h>

const DM_SIG_CFG_S idu_local_process[] = {
    {.sig_id = IDU_SIG_SAMPLE_PERIOD, .sig_size = 2, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_LABEL, .sig_size = 16, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = NULL},
#if !defined(CONFIG_IDU_AIDC_PROFILE)
    {.sig_id = IDU_SIG_SAVED_ADDRESS, .sig_size = 2, .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
#endif
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
    {.sig_id = IDU_SIG_RESERVED_ZERO, .sig_size = 32, .sig_flag = 0,
     .data_type = DATA_TYPE_U16ARRAY, .pf_sig_proc = idu_default_point},
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

BUILD_ASSERT(ARRAY_SIZE(idu_local_process) == IDU_LOCAL_PROCESS_COUNT,
             "Update IDU_LOCAL_PROCESS_COUNT in tables.h");

const DM_SIG_CFG_S idu_mvio_process[] = {
    {.sig_id = IDU_MVIO_DI, .sig_size = 2, .sig_flag = 0,
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
    {.sig_id = IDU_MVIO_PROTOCOL, .sig_size = 4, .sig_flag = 0,
     .data_type = DATA_TYPE_U32, .pf_sig_proc = NULL},
    {.sig_id = IDU_MVIO_ADDRESS, .sig_size = 2, .sig_flag = 0,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.sig_id = IDU_SIG_REMOTE_SETPOINT, .sig_size = 2,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT,
     .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL},
    {.sig_id = IDU_SIG_REMOTE_OFFSET, .sig_size = 4,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_COLLECT,
     .data_type = DATA_TYPE_S32, .pf_sig_proc = NULL},
#endif
};

BUILD_ASSERT(ARRAY_SIZE(idu_mvio_process) == IDU_MVIO_PROCESS_COUNT,
             "Update IDU_MVIO_PROCESS_COUNT in tables.h");

#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: points */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
const DM_SIG_CFG_S idu_imported_local_process[] = {
    {.sig_id=AIDC_LOCAL_SIG_TR_ADDR,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_VER_PUBLISH,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_VER_BUILD,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_VER_HOTFIX,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_VER_TAG,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DEV_SN,.sig_size=20,.sig_flag=17,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_point_alias},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT4_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT4_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT4_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT4_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT5_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT5_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT5_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT5_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT6_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT6_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT6_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT6_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT7_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT7_BAUD,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT7_PARITY,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT7_STOP_BITS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_WORKMODE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_MB_COM_STATUS,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_COM_ERR_CONFIG,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_MB_COM_ERR_TIME,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_MB_COM_REC_CONFIG,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_MB_COM_REC_TIME,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART1_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART1_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART2_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART2_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART3_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART3_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART4_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART4_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART5_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART5_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART6_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART6_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART7_ERR_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_MB_UART7_PKT_TIMES,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_TR_AES_KEY_VAL,.sig_size=16,.sig_flag=5,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_REDIRECT_SRC_PORT,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_REDIRECT_DST_PORT,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_REDIRECT_TIME,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT4_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT5_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT6_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT7_IFS,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL1_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL2_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL3_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL1_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL2_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL3_RANGE,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_serial_setting},
    {.sig_id=AIDC_LOCAL_SIG_CMM_MBTCP_SERVER_FLAG1,.sig_size=2,.sig_flag=17,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_CMM_MBTCP_SERVER_PORT1,.sig_size=2,.sig_flag=17,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DHCP_ENABLE,.sig_size=2,.sig_flag=17,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_ETH_IP,.sig_size=16,.sig_flag=25,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_ETH_MASK,.sig_size=16,.sig_flag=25,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_ETH_GATEWAY,.sig_size=16,.sig_flag=25,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DNS1,.sig_size=16,.sig_flag=25,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DNS2,.sig_size=16,.sig_flag=25,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_NUM,.sig_size=2,.sig_flag=17,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_DSP_ON_LINE_NUM,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_NTP_SERVER,.sig_size=64,.sig_flag=17,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_NTP_TIME_INTERVAL,.sig_size=4,.sig_flag=17,.data_type=DATA_TYPE_U32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_NTP_SYNC_TIME,.sig_size=32,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DEV_BRD_SEND_CNT,.sig_size=1,.sig_flag=17,.data_type=DATA_TYPE_U8,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_NOT_BOUND_DSP_NUM,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_1,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_2,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_3,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_4,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_5,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_6,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_7,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_8,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_9,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_10,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_11,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_12,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_13,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_14,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_15,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_16,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_275,.sig_size=16,.sig_flag=5,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_327,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_1062,.sig_size=2,.sig_flag=5,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_1481,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_16027,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_1621,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_1622,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_2500,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_protocol_identity},
    {.sig_id=AIDC_LOCAL_SIG_2502,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_U16ARRAY,.pf_sig_proc=idu_protocol_identity},
    {.sig_id=AIDC_LOCAL_SIG_2504,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_2516,.sig_size=16,.sig_flag=13,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_point_alias},
    {.sig_id=AIDC_LOCAL_SIG_2569,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_protocol_identity},
    {.sig_id=AIDC_LOCAL_SIG_2571,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_protocol_identity},
    {.sig_id=AIDC_LOCAL_SIG_2581,.sig_size=2,.sig_flag=8,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_protocol_identity},
    {.sig_id=AIDC_LOCAL_SIG_VERSION,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2597,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2612,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2627,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2642,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2657,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2672,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2687,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2702,.sig_size=30,.sig_flag=8,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_firmware_version},
    {.sig_id=AIDC_LOCAL_SIG_2717,.sig_size=30,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_2732,.sig_size=30,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_2747,.sig_size=30,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_5220,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_5378,.sig_size=20,.sig_flag=5,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_LOCAL_SIG_5487,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_S32,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_30034,.sig_size=2,.sig_flag=8,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_30098,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_31999,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_32000,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_32001,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_32003,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_32005,.sig_size=4,.sig_flag=8,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_unsupported_service},
    {.sig_id=AIDC_LOCAL_SIG_32007,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32009,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32019,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32052,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32053,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32054,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32056,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_U32,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32058,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16ARRAY,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_32061,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1301,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1302,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1303,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1304,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1305,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1306,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1307,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1308,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1309,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1310,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1311,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1312,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1313,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1314,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1315,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1316,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1317,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1318,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1319,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1320,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1321,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1322,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1323,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=1324,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_S16,.pf_sig_proc=idu_initialize_point},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_LOAD_CUR,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_R_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_S_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_T_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_INPUT_FREQ,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_TEMP,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_BAT_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_BAT_CHG_CUR,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_STATUS,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_MODULE_ID,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_POWER_MODE,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_LOAD_VOL_CTRL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_LOAD_CUR_LIMIT,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_BAT_VOL_CTRL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_BAT_CUR,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_LOAD_DEFAULT_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_BAT_DEFAULT_VOL,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
    {.sig_id=AIDC_LOCAL_SIG_SOFT_START,.sig_size=4,.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */
};
BUILD_ASSERT(ARRAY_SIZE(idu_imported_local_process) == IDU_IMPORTED_LOCAL_PROCESS_COUNT,
             "Update IDU_IMPORTED_LOCAL_PROCESS_COUNT in tables.h");
const DM_SIG_CFG_S idu_imported_dsp_process[] = {
    {.sig_id=AIDC_PARAM_PRODUCT_TYPE,.sig_size=30,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_PARAM_PRODUCT_CODE,.sig_size=16,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_PARAM_CLIENT_CODE,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_PARAM_TYPE,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_PARAM_MATRIL_CODE,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_PARAM_VEHICLE_TYPE,.sig_size=20,.sig_flag=0,.data_type=DATA_TYPE_STRING,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG1,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG2,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG3,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG4,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG1,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG2,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG3,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_MAIN_WRK_MODE,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_FAN_SPEED1,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_FAN_SPEED2,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_FAN_SPEED3,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_RECT1_VOL,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_LINE_VOL_RTO_S,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_LINE_VOL_STO_T,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_PHASE_VOL_R,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_PHASE_VOL_S,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_PHASE_VOL_T,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_BOOST_BUS_VOL,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_OUTPUT_VOL,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_BAT_VOL,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_F32_OUTPUT_CURR,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_INT_FAULT_WORD1,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_INT_FAULT_WORD2,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_INT_WARN_WORD1,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_U16_INT_WARN_WORD2,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_COM_CELL_WORK_CMD,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_COMT_BUCK_VOL_CMD,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_COM_BOOST_VOL_CMD,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_COM_CURR_LMT,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_AVERAGE_CURR,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ARM_COM_FORCE_BAT_WRK_EN,.sig_size=2,.sig_flag=0,.data_type=DATA_TYPE_U16,.pf_sig_proc=NULL},
    {.sig_id=AIDC_AC2_BAT_LOW_THRESH,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_AC2_BAT_HIGH_THRESH,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_GRID_FREQUENCY,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
    {.sig_id=AIDC_ALTITUDE_DEFAULT,.sig_size=4,.sig_flag=0,.data_type=DATA_TYPE_S32,.pf_sig_proc=NULL},
};
BUILD_ASSERT(ARRAY_SIZE(idu_imported_dsp_process) == IDU_IMPORTED_DSP_PROCESS_COUNT,
             "Update IDU_IMPORTED_DSP_PROCESS_COUNT in tables.h");
/* END IMPORTED TABLE: points */
#endif

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
#if !defined(CONFIG_IDU_AIDC_PROFILE)
    U16(IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS, 1, 1, 246),
#endif
    U16(IDU_DEV_LOCAL, 2503, 3, 0, UINT16_MAX),
    WORDS(IDU_DEV_LOCAL, 2504, 0x5347, 0x2d46, 0x4143, 0x544f, 0x5259, 0, 0, 0, 0, 0),
    WORDS(IDU_DEV_LOCAL, 2581, 0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000,
          0, 0, 0, 0, 0, 0, 0, 0),
    WORDS(IDU_DEV_LOCAL, 2568, 0x3031, 0x4152, 3, 0x0103),
    U16(IDU_DEV_LOCAL, 8149, IDU_DEVICE_TYPE, 0, UINT16_MAX),
    U32(IDU_DEV_LOCAL, IDU_SIG_PROTOCOL, IDU_PROTOCOL_VERSION),
    U16(IDU_DEV_LOCAL, 5486, 0x1204, 0, UINT16_MAX),
    U16(IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE, IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL), 0, UINT16_MAX),
    U16(IDU_DEV_LOCAL, IDU_SIG_ADDRESS, 1, 1, 16),
    U16(IDU_DEV_REMOTE_IO, IDU_SIG_ADDRESS, 1, 1, 247),
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    U16(IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT, 100, 0, 1000),
    {.dev_id = IDU_DEV_REMOTE_IO, .sig_id = IDU_SIG_REMOTE_OFFSET,
     .data = {.min_value.s32 = -100000, .max_value.s32 = 100000}, .restore = true},
#endif
#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: defaults */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_TR_ADDR,.data={.def_value.u32=1U,.min_value.u16=1,.max_value.u16=247},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_VER_PUBLISH,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_VER_BUILD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_VER_HOTFIX,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_VER_TAG,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DEV_SN,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT1_WORKMODE,.data={.def_value.u32=2U,.min_value.u32=1,.max_value.u32=5},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT1_BAUD,.data={.def_value.u32=165U,.min_value.u32=161,.max_value.u32=167},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT1_PARITY,.data={.def_value.u32=85U,.min_value.u32=85,.max_value.u32=162},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS,.data={.def_value.u32=161U,.min_value.u32=161,.max_value.u32=162},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT2_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT2_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT2_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT2_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT3_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT3_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT3_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT3_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT4_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT4_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT4_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT4_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT5_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT5_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT5_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT5_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT6_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT6_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT6_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT6_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT7_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT7_BAUD,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT7_PARITY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT7_STOP_BITS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_WORKMODE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_COM_STATUS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_COM_ERR_CONFIG,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_COM_ERR_TIME,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_COM_REC_CONFIG,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_COM_REC_TIME,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART1_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART1_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART2_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART2_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART3_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART3_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART4_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART4_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART5_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART5_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART6_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART6_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART7_ERR_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_MB_UART7_PKT_TIMES,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_TR_AES_KEY_VAL,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_REDIRECT_SRC_PORT,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_REDIRECT_DST_PORT,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_REDIRECT_TIME,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT1_IFS,.data={.def_value.u32=5U,.min_value.u32=1,.max_value.u32=1000},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT2_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT3_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT4_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT5_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT6_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_UART_PORT7_IFS,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL1_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL2_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT1_CHANNEL3_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL1_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL2_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CAN_PORT2_CHANNEL3_RANGE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CMM_MBTCP_SERVER_FLAG1,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_CMM_MBTCP_SERVER_PORT1,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_DHCP_ENABLE,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_IP,.data={.def_value.pst=(void *)(const uint16_t[]){12594,11825,12846,12594,11825,12800,0,0}},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_MASK,.data={.def_value.pst=(void *)(const uint16_t[]){12853,13614,12334,12334,12288,0,0,0}},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_GATEWAY,.data={.def_value.pst=(void *)(const uint16_t[]){12594,11825,12846,12594,11825,0,0,0}},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_DNS1,.data={.def_value.pst=(void *)(const uint16_t[]){12590,12846,13358,14336,0,0,0,0}},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_ETH_DNS2,.data={.def_value.pst=(void *)(const uint16_t[]){14382,14382,14382,14336,0,0,0,0}},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_NUM,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_ON_LINE_NUM,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_NTP_SERVER,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_NTP_TIME_INTERVAL,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_NTP_SYNC_TIME,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DEV_BRD_SEND_CNT,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_NOT_BOUND_DSP_NUM,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_1,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_2,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_3,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_4,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_5,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_6,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_7,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_8,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_9,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_10,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_11,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_12,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_13,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_14,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_15,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_16,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_275,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_327,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_1062,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_1481,.data={.def_value.u32=8506U},.restore=true},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_16027,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_1621,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_1622,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2500,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2502,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2504,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2516,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2569,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2571,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2581,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_VERSION,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2597,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2612,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2627,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2642,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2657,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2672,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2687,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2702,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2717,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2732,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_2747,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_5220,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_5378,.data={.def_value.pst=(void *)(const uint16_t[]){0,0,0,0,0,0,0,0,0,0}},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_5487,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_30034,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_30098,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_31999,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32000,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32001,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32003,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32005,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32007,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32009,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32019,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32052,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32053,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32054,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32056,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32058,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=AIDC_LOCAL_SIG_32061,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1301,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1302,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1303,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1304,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1305,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1306,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1307,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1308,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1309,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1310,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1311,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1312,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1313,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1314,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1315,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1316,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1317,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1318,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1319,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1320,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1321,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1322,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1323,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=247,.sig_id=1324,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=11,.sig_id=AIDC_PARAM_PRODUCT_TYPE,.data={.def_value.pst=(void *)(const uint16_t[]){19777,19521,17224,18772,17709,21343,12339,12337,12590,12337,11824,12288,0,0,0}},.restore=true},
    {.dev_id=11,.sig_id=AIDC_PARAM_PRODUCT_CODE,.data={.def_value.pst=(void *)(const uint16_t[]){12288,0,0,0,0,0,0,0}},.restore=true},
    {.dev_id=11,.sig_id=AIDC_PARAM_CLIENT_CODE,.data={.def_value.pst=(void *)(const uint16_t[]){12288,0,0,0,0,0,0,0,0,0}},.restore=true},
    {.dev_id=11,.sig_id=AIDC_PARAM_TYPE,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_PARAM_MATRIL_CODE,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_PARAM_VEHICLE_TYPE,.data={.def_value.pst=(void *)(const uint16_t[]){12288,0,0,0,0,0,0,0,0,0}},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG1,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG2,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG3,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_FW_FLT_FLG4,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG1,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG2,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_DCDC_TSK_WARN_FLT_FLG3,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_MAIN_WRK_MODE,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=500},.restore=true},
    {.dev_id=11,.sig_id=AIDC_U16_FAN_SPEED1,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=11,.sig_id=AIDC_U16_FAN_SPEED2,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=11,.sig_id=AIDC_U16_FAN_SPEED3,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=11,.sig_id=AIDC_F32_RECT1_VOL,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_LINE_VOL_RTO_S,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_LINE_VOL_STO_T,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_PHASE_VOL_R,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_PHASE_VOL_S,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_PHASE_VOL_T,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_BOOST_BUS_VOL,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_OUTPUT_VOL,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_BAT_VOL,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_F32_OUTPUT_CURR,.data={.def_value.u32=0U,.min_value.s32=-100,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_U16_INT_FAULT_WORD1,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_U16_INT_FAULT_WORD2,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_U16_INT_WARN_WORD1,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_U16_INT_WARN_WORD2,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_COM_CELL_WORK_CMD,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=1},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_COMT_BUCK_VOL_CMD,.data={.def_value.u32=0U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_COM_BOOST_VOL_CMD,.data={.def_value.u32=0U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_COM_CURR_LMT,.data={.def_value.u32=0U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_AVERAGE_CURR,.data={.def_value.u32=0U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_ARM_COM_FORCE_BAT_WRK_EN,.data={.def_value.u32=0U,.min_value.u16=0,.max_value.u16=1},.restore=true},
    {.dev_id=11,.sig_id=AIDC_AC2_BAT_LOW_THRESH,.data={.def_value.u32=500U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_AC2_BAT_HIGH_THRESH,.data={.def_value.u32=900U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
    {.dev_id=11,.sig_id=AIDC_GRID_FREQUENCY,.data={.def_value.u32=0U},.restore=false},
    {.dev_id=11,.sig_id=AIDC_ALTITUDE_DEFAULT,.data={.def_value.u32=1000U,.min_value.s32=0,.max_value.s32=65535},.restore=true},
/* END IMPORTED TABLE: defaults */
#endif
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
