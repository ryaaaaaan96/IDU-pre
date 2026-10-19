#include "idu_equip.h"
#include "../models/mvio.h"
#include "../instances.h"

IDU_MVIO_COLLECT_FIELDS(mvio);

#define MVIO_COLLECT(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    IDU_MVIO_COLLECT(mvio, dev, period, identity),

/* Placeholder downstream device uses the known MV-IO map. Replace these
 * blocks with the actual downstream equipment's register specification.
 */
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/fields.inc"
#endif
static const struct co_cfg table[] = {
    IDU_MVIO_INSTANCES(MVIO_COLLECT)
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/collect.inc"
#endif
};

const struct co_cfg *idu_collect_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
