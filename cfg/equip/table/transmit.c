#include "idu_equip.h"

#define MAP(fc, reg, words, dev, sig) \
    {.function = fc, .address = reg, .count = words, .key = {dev, sig}}
#define ZERO(reg, words) \
    {.function = 4, .address = reg, .count = words, .kind = IDU_MAP_ZERO}
#define CURRENT(n) MAP(4, 8163 + n, 1, IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n)
#define REMOTE_CURRENT(n) MAP(4, 10001 + n, 1, IDU_DEV_REMOTE_IO, IDU_SIG_CURRENT_1 + n)
#define QUALITY(reg, sig) \
    {.function = 4, .address = reg, .count = 1, \
     .key = {IDU_DEV_REMOTE_IO, sig}, .kind = IDU_MAP_QUALITY}

static const struct idu_transmit_cfg table[] = {
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/transmit.inc"
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
#else
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
    MAP(4, 10000, 1, IDU_DEV_REMOTE_IO, IDU_SIG_DI),
    REMOTE_CURRENT(0), REMOTE_CURRENT(1), REMOTE_CURRENT(2), REMOTE_CURRENT(3),
    REMOTE_CURRENT(4), REMOTE_CURRENT(5), REMOTE_CURRENT(6), REMOTE_CURRENT(7),
    QUALITY(10009, IDU_SIG_DI),
    MAP(4, 10010, 2, IDU_DEV_REMOTE_IO, IDU_SIG_PROTOCOL),
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
    QUALITY(10014, IDU_SIG_CURRENT_1 + 0), QUALITY(10015, IDU_SIG_CURRENT_1 + 1),
    QUALITY(10016, IDU_SIG_CURRENT_1 + 2), QUALITY(10017, IDU_SIG_CURRENT_1 + 3),
    QUALITY(10018, IDU_SIG_CURRENT_1 + 4), QUALITY(10019, IDU_SIG_CURRENT_1 + 5),
    QUALITY(10020, IDU_SIG_CURRENT_1 + 6), QUALITY(10021, IDU_SIG_CURRENT_1 + 7),
    QUALITY(10022, IDU_SIG_PROTOCOL), QUALITY(10023, IDU_SIG_ADDRESS),
    MAP(3, 30460, 1, IDU_DEV_REMOTE_IO, IDU_SIG_ADDRESS),
#endif
};

const struct idu_transmit_cfg *idu_transmit_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
