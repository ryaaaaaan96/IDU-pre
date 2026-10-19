#include "signal_ids.h"
#include "device_config.h"
#include "../equipment/models/mvio.h"
#include "../equipment/instances.h"

#define MAP(fc, reg, words, dev, sig) \
    {.function = fc, .address = reg, .count = words, .key = {dev, sig}}
#define ZERO(reg, words) \
    {.function = 4, .address = reg, .count = words, .kind = DP_MAP_ZERO}
#define CURRENT(n) MAP(4, 8163 + n, 1, IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n)

#define MVIO_TRANSMIT(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    IDU_MVIO_TRANSMIT(dev, north, address_reg),

static const struct dp_transmit_cfg table[] = {
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "generated/transmit.inc"
    MAP(4, 10012, 1, IDU_DEV_LOCAL, IDU_SIG_VIRTUAL_MODE),
#else
#include "transmit_local.inc"

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
