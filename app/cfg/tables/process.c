#include "imported_signals.h"
#include "device.h"
#include "signal_ids.h"
#include "mvio_signals.h"
#include "device_instances.h"
#include "process/idu_process.h"
#include <zephyr/sys/util.h>

#define SIGNAL(id, bytes, flags, type, callback) \
    {.sig_id = id, .sig_size = bytes, .sig_flag = flags, \
     .data_type = type, .pf_sig_proc = callback}
#define SAVED (FLAG_PERSISTENCE | FLAG_RT_STORE)
#define CURRENT(n) SIGNAL(IDU_SIG_CURRENT_1 + n, 2, 0, DATA_TYPE_U16, NULL)
#define IDU_IDENTITY(id, bytes, type) SIGNAL(id, bytes, 0, type, idu_default_point)

static const DM_SIG_CFG_S local[] = {
    SIGNAL(IDU_SIG_SAMPLE_PERIOD, 2, SAVED, DATA_TYPE_U16, NULL),
    SIGNAL(IDU_SIG_LABEL, 16, SAVED, DATA_TYPE_U16ARRAY, NULL),
#if !defined(CONFIG_IDU_AIDC_PROFILE)
    SIGNAL(IDU_SIG_SAVED_ADDRESS, 2, SAVED, DATA_TYPE_U16, NULL),
#endif
    IDU_IDENTITY(2503, 2, DATA_TYPE_U16),
    IDU_IDENTITY(2504, 20, DATA_TYPE_U16ARRAY),
    IDU_IDENTITY(2581, 30, DATA_TYPE_U16ARRAY),
    IDU_IDENTITY(2568, 8, DATA_TYPE_U16ARRAY),
    IDU_IDENTITY(8149, 2, DATA_TYPE_U16),
    IDU_IDENTITY(IDU_SIG_PROTOCOL, 4, DATA_TYPE_U32),
    IDU_IDENTITY(5486, 2, DATA_TYPE_U16),
    IDU_IDENTITY(IDU_SIG_VIRTUAL_MODE, 2, DATA_TYPE_U16),
    IDU_IDENTITY(IDU_SIG_RESERVED_ZERO, 32, DATA_TYPE_U16ARRAY),
    SIGNAL(IDU_SIG_ADDRESS, 2, 0, DATA_TYPE_U16, NULL),
    SIGNAL(IDU_SIG_DI, 2, 0, DATA_TYPE_U16, NULL),
    CURRENT(0), CURRENT(1), CURRENT(2), CURRENT(3),
    CURRENT(4), CURRENT(5), CURRENT(6), CURRENT(7),
};

static const DM_SIG_CFG_S mvio[] = {
    SIGNAL(IDU_MVIO_DI, 2, 0, DATA_TYPE_U16, NULL),
    CURRENT(0), CURRENT(1), CURRENT(2), CURRENT(3),
    CURRENT(4), CURRENT(5), CURRENT(6), CURRENT(7),
    SIGNAL(IDU_MVIO_PROTOCOL, 4, 0, DATA_TYPE_U32, NULL),
    SIGNAL(IDU_MVIO_ADDRESS, 2, 0, DATA_TYPE_U16, NULL),
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    SIGNAL(IDU_SIG_REMOTE_SETPOINT, 2, SAVED | FLAG_COLLECT, DATA_TYPE_U16, NULL),
    SIGNAL(IDU_SIG_REMOTE_OFFSET, 4, SAVED | FLAG_COLLECT, DATA_TYPE_S32, NULL),
#endif
};

#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: points */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
static const DM_SIG_CFG_S imported_local[] = {
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
static const DM_SIG_CFG_S imported_dsp[] = {
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
/* END IMPORTED TABLE: points */
#endif

const DM_SIG_CFG_S *idu_process_table(uint8_t dev, uint16_t *count)
{
    if (dev == IDU_DEV_LOCAL) { *count = ARRAY_SIZE(local); return local; }
#define MVIO_TABLE(id, bus, unit, north, address_reg) \
    if (dev == id) { *count = ARRAY_SIZE(mvio); return mvio; }
    IDU_MVIO_INSTANCES(MVIO_TABLE)
#undef MVIO_TABLE
#if defined(CONFIG_IDU_AIDC_PROFILE)
    if (dev == 247) { *count = ARRAY_SIZE(imported_local); return imported_local; }
    if (dev >= 11 && dev <= 18) { *count = ARRAY_SIZE(imported_dsp); return imported_dsp; }
#endif
    *count = 0;
    return NULL;
}
