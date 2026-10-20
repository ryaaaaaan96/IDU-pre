#include "imported_signals.h"
#include "device.h"
#include "signal_ids.h"
#include "device_instances.h"
#include <zephyr/sys/util.h>

/* The reference process row has no default/range fields. Keep its companion
 * DM_SIG_DATA_S records here, supplied through the original range callback.
 */
struct point_default {
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
static const struct point_default defaults[] = {
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
};

#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: defaults */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
static const struct point_default imported_defaults[] = {
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
};
/* END IMPORTED TABLE: defaults */
#endif

static uint16_t default_device(uint16_t dev)
{
#define MVIO_DEFAULT(id, bus, unit, north, address_reg) \
    if (dev == id) { return IDU_DEV_REMOTE_IO; }
    IDU_MVIO_INSTANCES(MVIO_DEFAULT)
#undef MVIO_DEFAULT
#if defined(CONFIG_IDU_AIDC_PROFILE)
    if (dev >= 11 && dev <= 18) { return 11; }
#endif
    return dev;
}

static const struct point_default *find_default(uint16_t dev, uint16_t sig)
{
    dev = default_device(dev);
    for (size_t i = 0; i < ARRAY_SIZE(defaults); ++i) {
        if (defaults[i].dev_id == dev && defaults[i].sig_id == sig) { return &defaults[i]; }
    }
#if defined(CONFIG_IDU_AIDC_PROFILE)
    for (size_t i = 0; i < ARRAY_SIZE(imported_defaults); ++i) {
        if (imported_defaults[i].dev_id == dev && imported_defaults[i].sig_id == sig) {
            return &imported_defaults[i];
        }
    }
#endif
    return NULL;
}

int idu_point_default(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *value, bool *recover)
{
    uint16_t count;
    const DM_SIG_CFG_S *rows = idu_process_table(dev, &count);
    const struct point_default *entry = find_default(dev, sig);
    *recover = false; /* Never force defaults over an existing NVS snapshot. */
    if (entry) { *value = entry->data; return 0; }
    for (size_t i = 0; i < count; ++i) {
        if (rows[i].sig_id == sig) {
            *value = (DM_SIG_DATA_S){0};
            if (rows[i].sig_size > 4) { value->def_value.pst = (void *)zeros; }
            return 0;
        }
    }
    return -1;
}

bool idu_point_restore(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value)
{
    ARG_UNUSED(size); ARG_UNUSED(value);
    const struct point_default *entry = find_default(dev, sig);
    return !entry || entry->restore;
}
