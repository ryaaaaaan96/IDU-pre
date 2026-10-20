#include "imported_signals.h"
#include "signal_ids.h"
#include "device.h"
#include "mvio_signals.h"
#include "device_instances.h"

#define MAP(fc, reg, words, dev, sig) \
    {.sig_id = sig, .reg_addr = reg, .reg_num = words, .dev_id = dev, .reg_type = fc, \
     .byte_order = BYTE_ORDER_DEFAULT}
#define ZERO(reg, words) \
    MAP(4, reg, words, IDU_DEV_LOCAL, IDU_SIG_RESERVED_ZERO)
#define CURRENT(n) MAP(4, 8163 + n, 1, IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n)

#define REMOTE_CURRENT(dev, base, n) MAP(4, base + 1 + n, 1, dev, IDU_MVIO_CURRENT_1 + n)
#define MVIO_TRANSMIT(dev, bus, unit, north, address_reg) \
    MAP(4, north, 1, dev, IDU_MVIO_DI), \
    REMOTE_CURRENT(dev, north, 0), REMOTE_CURRENT(dev, north, 1), \
    REMOTE_CURRENT(dev, north, 2), REMOTE_CURRENT(dev, north, 3), \
    REMOTE_CURRENT(dev, north, 4), REMOTE_CURRENT(dev, north, 5), \
    REMOTE_CURRENT(dev, north, 6), REMOTE_CURRENT(dev, north, 7), \
    MAP(4, north + 10, 2, dev, IDU_MVIO_PROTOCOL), \
    MAP(3, address_reg, 1, dev, IDU_MVIO_ADDRESS),

static const MB_TABLE_CELL_T table[] = {
#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: transmit */
/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {.sig_id=AIDC_LOCAL_SIG_TR_ADDR,.reg_addr=41654,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_VER_PUBLISH,.reg_addr=2815,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_VER_BUILD,.reg_addr=2816,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_VER_HOTFIX,.reg_addr=2817,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DEV_SN,.reg_addr=31811,.reg_num=10,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_BAUD,.reg_addr=41655,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_PARITY,.reg_addr=41657,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS,.reg_addr=41659,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_BAUD,.reg_addr=41661,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_PARITY,.reg_addr=41663,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT2_STOP_BITS,.reg_addr=41665,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_BAUD,.reg_addr=41667,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_PARITY,.reg_addr=41669,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_UART_PORT3_STOP_BITS,.reg_addr=41671,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DHCP_ENABLE,.reg_addr=41851,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_IP,.reg_addr=41852,.reg_num=8,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_MASK,.reg_addr=41860,.reg_num=8,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_GATEWAY,.reg_addr=41868,.reg_num=8,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DNS1,.reg_addr=41876,.reg_num=8,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_ETH_DNS2,.reg_addr=41884,.reg_num=8,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_NUM,.reg_addr=41651,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_ON_LINE_NUM,.reg_addr=16409,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_ON_LINE_NUM,.reg_addr=30097,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_ON_LINE_NUM,.reg_addr=31806,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_NTP_SERVER,.reg_addr=41896,.reg_num=32,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_NTP_TIME_INTERVAL,.reg_addr=41894,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_NTP_SYNC_TIME,.reg_addr=41928,.reg_num=16,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_NOT_BOUND_DSP_NUM,.reg_addr=31916,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_1,.reg_addr=41689,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_2,.reg_addr=41699,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_3,.reg_addr=41709,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_4,.reg_addr=41719,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_5,.reg_addr=41729,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_6,.reg_addr=41739,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_7,.reg_addr=41749,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_8,.reg_addr=41759,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_9,.reg_addr=41769,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_10,.reg_addr=41779,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_11,.reg_addr=41789,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_12,.reg_addr=41799,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_13,.reg_addr=41809,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_14,.reg_addr=41819,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_15,.reg_addr=41829,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_DSP_DEV_SN_16,.reg_addr=41839,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_1481,.reg_addr=2515,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_1621,.reg_addr=30078,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_1622,.reg_addr=30079,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2500,.reg_addr=2500,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2502,.reg_addr=2502,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2504,.reg_addr=2504,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2516,.reg_addr=2516,.reg_num=8,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2569,.reg_addr=2569,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2571,.reg_addr=2571,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2581,.reg_addr=2581,.reg_num=1,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_VERSION,.reg_addr=2582,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_VERSION,.reg_addr=31821,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2597,.reg_addr=2597,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2612,.reg_addr=2612,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2627,.reg_addr=2627,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2642,.reg_addr=2642,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2657,.reg_addr=2657,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2672,.reg_addr=2672,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2687,.reg_addr=2687,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2702,.reg_addr=2702,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2717,.reg_addr=2717,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2732,.reg_addr=2732,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_2747,.reg_addr=2747,.reg_num=15,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_5378,.reg_addr=2505,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_5378,.reg_addr=4990,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_5378,.reg_addr=5378,.reg_num=10,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_5487,.reg_addr=5487,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_5487,.reg_addr=31997,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_30034,.reg_addr=30034,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_30098,.reg_addr=30098,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_31999,.reg_addr=31999,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32000,.reg_addr=32000,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32001,.reg_addr=32001,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32003,.reg_addr=32003,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32005,.reg_addr=32005,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32007,.reg_addr=32007,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32009,.reg_addr=32009,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32019,.reg_addr=32019,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32052,.reg_addr=32052,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32053,.reg_addr=32053,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32054,.reg_addr=32054,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32056,.reg_addr=32056,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32058,.reg_addr=32058,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_32061,.reg_addr=32061,.reg_num=1,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_VOL,.reg_addr=43001,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_CUR,.reg_addr=43002,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_R_VOL,.reg_addr=43003,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_S_VOL,.reg_addr=43004,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_T_VOL,.reg_addr=43005,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_INPUT_FREQ,.reg_addr=43006,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_TEMP,.reg_addr=43007,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_BAT_VOL,.reg_addr=43008,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_BAT_CHG_CUR,.reg_addr=43009,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_STATUS,.reg_addr=43010,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_MODULE_ID,.reg_addr=43011,.reg_num=2,.dev_id=247,.reg_type=4,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_POWER_MODE,.reg_addr=43012,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_VOL_CTRL,.reg_addr=43013,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_CUR_LIMIT,.reg_addr=43014,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_BAT_VOL_CTRL,.reg_addr=43015,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_BAT_CUR,.reg_addr=43016,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_LOAD_DEFAULT_VOL,.reg_addr=43017,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_BAT_DEFAULT_VOL,.reg_addr=43018,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
    {.sig_id=AIDC_LOCAL_SIG_SOFT_START,.reg_addr=43019,.reg_num=2,.dev_id=247,.reg_type=3,.byte_order=BYTE_ORDER_DEFAULT},
/* END IMPORTED TABLE: transmit */
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
#else
/* Local MV-IO protocol windows and demo parameter mappings, in lookup order. */
    MAP(3, 31000, 1, IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD),
    MAP(3, 31001, 8, IDU_DEV_LOCAL, IDU_SIG_LABEL),
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    MAP(3, 31500, 1, IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT),
    MAP(3, 31501, 2, IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_OFFSET),
#endif
    MAP(4, 2503, 1, IDU_DEV_LOCAL, 2503),
    MAP(4, 2504, 10, IDU_DEV_LOCAL, 2504),
    MAP(4, 4989, 10, IDU_DEV_LOCAL, 2504),
    MAP(4, 4805, 8, IDU_DEV_LOCAL, 2504),
    MAP(4, 2581, 15, IDU_DEV_LOCAL, 2581),
    MAP(4, 2568, 4, IDU_DEV_LOCAL, 2568),
    MAP(4, 8149, 1, IDU_DEV_LOCAL, 8149),
    MAP(4, 4813, 1, IDU_DEV_LOCAL, 8149),
    MAP(4, 4814, 1, IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS),
    MAP(4, 8150, 2, IDU_DEV_LOCAL, IDU_SIG_PROTOCOL),
    MAP(4, 8152, 1, IDU_DEV_LOCAL, IDU_SIG_DI),
    CURRENT(0), CURRENT(1), CURRENT(2), CURRENT(3),
    CURRENT(4), CURRENT(5), CURRENT(6), CURRENT(7),
    MAP(4, 5486, 1, IDU_DEV_LOCAL, 5486),
    ZERO(2514, 16), ZERO(2530, 3), ZERO(4999, 16), ZERO(5487, 1),
    ZERO(8153, 10), ZERO(8171, 16), ZERO(8187, 13),
    MAP(3, 30459, 1, IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS),

    /* IDU extension: separate remote data from physical local inputs. */
    IDU_MVIO_INSTANCES(MVIO_TRANSMIT)
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
#endif
};

const MB_TABLE_CELL_T *idu_transmit_table(uint16_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
