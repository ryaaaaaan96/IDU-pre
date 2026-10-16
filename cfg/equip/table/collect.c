#include "idu_equip.h"

static const struct idu_collect_field di[] = {
    {.sig_id = IDU_SIG_DI, .width = 1},
};
#define CURRENT(n) {.sig_id = IDU_SIG_CURRENT_1 + n, .offset = n, .width = 1}
static const struct idu_collect_field currents[] = {
    CURRENT(0), CURRENT(1), CURRENT(2), CURRENT(3),
    CURRENT(4), CURRENT(5), CURRENT(6), CURRENT(7),
};
static const struct idu_collect_field protocol[] = {
    {.sig_id = IDU_SIG_PROTOCOL, .width = 2, .order = IDU_LOW_WORD_FIRST},
};
static const struct idu_collect_field address[] = {
    {.sig_id = IDU_SIG_ADDRESS, .width = 1},
};

/* Placeholder downstream device uses the known MV-IO map. Replace these
 * blocks with the actual downstream equipment's register specification.
 */
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/fields.inc"
#endif
static const struct idu_collect_cfg table[] = {
    {.dev_id = IDU_DEV_REMOTE_IO, .function = 4, .address = 8152, .count = 1,
     .period_ms = 500, .fields = di, .field_count = ARRAY_SIZE(di)},
    {.dev_id = IDU_DEV_REMOTE_IO, .function = 4, .address = 8163, .count = 8,
     .period_ms = 500, .fields = currents, .field_count = ARRAY_SIZE(currents)},
    {.dev_id = IDU_DEV_REMOTE_IO, .function = 4, .address = 8150, .count = 2,
     .period_ms = 5000, .fields = protocol, .field_count = ARRAY_SIZE(protocol)},
    {.dev_id = IDU_DEV_REMOTE_IO, .function = 3, .address = 30459, .count = 1,
     .period_ms = 5000, .fields = address, .field_count = ARRAY_SIZE(address)},
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/collect.inc"
#endif
};

const struct idu_collect_cfg *idu_collect_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
