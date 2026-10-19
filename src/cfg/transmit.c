#include "signal_ids.h"
#include "device_config.h"
#include "mvio_model.h"
#include "instances.h"

#define MAP(fc, reg, words, dev, sig) \
    {.function = fc, .address = reg, .count = words, .key = {dev, sig}}
#define ZERO(reg, words) \
    {.function = 4, .address = reg, .count = words, .kind = DP_MAP_ZERO}
#define CURRENT(n) MAP(4, 8163 + n, 1, IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n)

#define MVIO_TRANSMIT(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    IDU_MVIO_TRANSMIT(dev, north, address_reg),

static const struct dp_transmit_cfg table[] = {
#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: transmit */
/* Generated from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {.function=3,.address=41654,.count=1,.key={247,1},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2815,.count=1,.key={247,6},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2816,.count=1,.key={247,7},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2817,.count=1,.key={247,8},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=31811,.count=10,.key={247,14},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41655,.count=2,.key={247,102},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41657,.count=2,.key={247,103},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41659,.count=2,.key={247,104},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41661,.count=2,.key={247,106},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41663,.count=2,.key={247,107},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41665,.count=2,.key={247,108},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41667,.count=2,.key={247,110},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41669,.count=2,.key={247,111},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41671,.count=2,.key={247,112},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41851,.count=1,.key={247,264},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41852,.count=8,.key={247,265},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41860,.count=8,.key={247,266},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41868,.count=8,.key={247,267},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41876,.count=8,.key={247,268},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41884,.count=8,.key={247,269},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41651,.count=1,.key={247,177},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=16409,.count=1,.key={247,178},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=30097,.count=1,.key={247,178},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=31806,.count=1,.key={247,178},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41896,.count=32,.key={247,179},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41894,.count=2,.key={247,180},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=41928,.count=16,.key={247,181},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=31916,.count=1,.key={247,329},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41689,.count=10,.key={247,330},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41699,.count=10,.key={247,331},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41709,.count=10,.key={247,332},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41719,.count=10,.key={247,333},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41729,.count=10,.key={247,334},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41739,.count=10,.key={247,335},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41749,.count=10,.key={247,336},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41759,.count=10,.key={247,337},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41769,.count=10,.key={247,338},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41779,.count=10,.key={247,339},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41789,.count=10,.key={247,340},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41799,.count=10,.key={247,341},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41809,.count=10,.key={247,342},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41819,.count=10,.key={247,343},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41829,.count=10,.key={247,344},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=41839,.count=10,.key={247,345},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2515,.count=1,.key={247,1481},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=30078,.count=1,.key={247,1621},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=30079,.count=1,.key={247,1622},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2500,.count=2,.key={247,2500},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2502,.count=2,.key={247,2502},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2504,.count=1,.key={247,2504},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2516,.count=8,.key={247,2516},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2569,.count=2,.key={247,2569},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2571,.count=2,.key={247,2571},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2581,.count=1,.key={247,2581},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2582,.count=15,.key={247,2582},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=31821,.count=15,.key={247,2582},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2597,.count=15,.key={247,2597},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2612,.count=15,.key={247,2612},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2627,.count=15,.key={247,2627},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2642,.count=15,.key={247,2642},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2657,.count=15,.key={247,2657},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2672,.count=15,.key={247,2672},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2687,.count=15,.key={247,2687},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2702,.count=15,.key={247,2702},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2717,.count=15,.key={247,2717},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2732,.count=15,.key={247,2732},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2747,.count=15,.key={247,2747},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=2505,.count=10,.key={247,5378},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=4990,.count=10,.key={247,5378},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=5378,.count=10,.key={247,5378},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=5487,.count=2,.key={247,5487},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=31997,.count=2,.key={247,5487},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=30034,.count=1,.key={247,30034},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=30098,.count=1,.key={247,30098},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=31999,.count=1,.key={247,31999},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32000,.count=1,.key={247,32000},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32001,.count=2,.key={247,32001},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32003,.count=2,.key={247,32003},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32005,.count=2,.key={247,32005},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32007,.count=2,.key={247,32007},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32009,.count=1,.key={247,32009},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32019,.count=1,.key={247,32019},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32052,.count=1,.key={247,32052},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32053,.count=1,.key={247,32053},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32054,.count=2,.key={247,32054},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32056,.count=2,.key={247,32056},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32058,.count=1,.key={247,32058},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=32061,.count=1,.key={247,32061},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43001,.count=2,.key={247,1001},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43002,.count=2,.key={247,1002},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43003,.count=2,.key={247,1003},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43004,.count=2,.key={247,1004},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43005,.count=2,.key={247,1005},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43006,.count=2,.key={247,1006},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43007,.count=2,.key={247,1007},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43008,.count=2,.key={247,1008},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43009,.count=2,.key={247,1009},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43010,.count=2,.key={247,1010},.order=DP_LOW_WORD_FIRST},
    {.function=4,.address=43011,.count=2,.key={247,1011},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43012,.count=2,.key={247,1012},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43013,.count=2,.key={247,1013},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43014,.count=2,.key={247,1014},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43015,.count=2,.key={247,1015},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43016,.count=2,.key={247,1016},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43017,.count=2,.key={247,1017},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43018,.count=2,.key={247,1018},.order=DP_LOW_WORD_FIRST},
    {.function=3,.address=43019,.count=2,.key={247,1019},.order=DP_LOW_WORD_FIRST},
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
    ZERO(2514, 19), ZERO(4999, 16), ZERO(5487, 1),
    ZERO(8153, 10), ZERO(8171, 29),
    MAP(3, 30459, 1, IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS),

    /* IDU extension: separate remote data from physical local inputs. */
    IDU_MVIO_INSTANCES(MVIO_TRANSMIT)
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
#endif
};

const struct dp_transmit_cfg *idu_transmit_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
