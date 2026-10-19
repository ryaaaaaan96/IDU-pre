#include "idu_equip.h"
#include "idu_pre.h"
#include "idu_mvio.h"
#include "../models/mvio.h"
#include "../instances.h"
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif
#include <errno.h>

static int current_process(const struct dp_point_cfg *cfg, struct dp_value *value,
                           const struct dp_value *old_value)
{
    ARG_UNUSED(cfg);
    ARG_UNUSED(old_value);
    if (value->words[0] > 4095) {
        return -ERANGE;
    }
    value->words[0] = idu_current_from_raw(value->words[0]);
    return 0;
}

#define CONST_U16(sig, val) \
    {.key = {IDU_DEV_LOCAL, sig}, .source = DP_SOURCE_CONSTANT, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .initial.words = {val}}
#define LOCAL_CURRENT(n) \
    {.key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n}, .source = DP_SOURCE_LOCAL, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 1500, \
     .process = current_process}

#define PARAM_FLAGS (DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE)

#define MVIO_POINTS(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    IDU_MVIO_POINTS(dev, stale, identity_stale),

static const struct dp_point_cfg table[] = {
    {.key = {IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD}, .source = DP_SOURCE_PARAMETER,
     .type = DP_U16, .width = 1, .minimum = 100, .maximum = 1000,
     .initial.words = {CONFIG_IDU_SAMPLE_PERIOD_MS}, .param_flags = PARAM_FLAGS},
    {.key = {IDU_DEV_LOCAL, IDU_SIG_LABEL}, .source = DP_SOURCE_PARAMETER,
     .type = DP_WORDS, .width = 8, .initial.words = {0x4944, 0x552d, 0x7072, 0x6500},
     .param_flags = PARAM_FLAGS},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    /* Demo writable registers: replace only after the real slave map is known. */
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT}, .source = DP_SOURCE_PARAMETER,
     .type = DP_U16, .width = 1, .maximum = 1000, .initial.words = {100},
     .param_flags = PARAM_FLAGS | DP_PARAM_FORWARD, .write_address = 40000},
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_OFFSET}, .source = DP_SOURCE_PARAMETER,
     .type = DP_S32, .width = 2, .minimum = -100000, .maximum = 100000,
     .param_flags = PARAM_FLAGS | DP_PARAM_FORWARD, .write_address = 40001},
#endif
#if !defined(CONFIG_IDU_AIDC_PROFILE)
    {.key = {IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS}, .source = DP_SOURCE_PARAMETER,
     .type = DP_U16, .width = 1, .maximum = 246, .initial.words = {1},
     .param_flags = PARAM_FLAGS, .param_added = true,
     .seed_missing = idu_mvio_seed_address, .prepare_write = idu_mvio_prepare_address},
#endif
    CONST_U16(2503, 3),
    {.key = {IDU_DEV_LOCAL, 2504}, .source = DP_SOURCE_CONSTANT,
     .type = DP_WORDS, .width = 10,
     .initial.words = {0x5347, 0x2d46, 0x4143, 0x544f, 0x5259}}, /* SG-FACTORY */
    {.key = {IDU_DEV_LOCAL, 2581}, .source = DP_SOURCE_CONSTANT,
     .type = DP_WORDS, .width = 15,
     .initial.words = {0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000}},
    {.key = {IDU_DEV_LOCAL, 2568}, .source = DP_SOURCE_CONSTANT,
     .type = DP_WORDS, .width = 4, .initial.words = {0x3031, 0x4152, 3, 0x0103}},
    CONST_U16(8149, IDU_DEVICE_TYPE),
    {.key = {IDU_DEV_LOCAL, IDU_SIG_PROTOCOL}, .source = DP_SOURCE_CONSTANT,
     .type = DP_U32, .width = 2, .maximum = UINT32_MAX,
     .initial.words = {IDU_PROTOCOL_VERSION & 0xffff, IDU_PROTOCOL_VERSION >> 16}},
    CONST_U16(5486, 0x1204),
    CONST_U16(IDU_SIG_VIRTUAL_MODE, IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL)),
    {.key = {IDU_DEV_LOCAL, IDU_SIG_ADDRESS}, .source = DP_SOURCE_LOCAL,
     .type = DP_U16, .width = 1, .minimum = 1, .maximum = 16, .stale_ms = 1500},
    {.key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .source = DP_SOURCE_LOCAL,
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 1500},
    LOCAL_CURRENT(0), LOCAL_CURRENT(1), LOCAL_CURRENT(2), LOCAL_CURRENT(3),
    LOCAL_CURRENT(4), LOCAL_CURRENT(5), LOCAL_CURRENT(6), LOCAL_CURRENT(7),

    IDU_MVIO_INSTANCES(MVIO_POINTS)
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/points.inc"
#endif
};

const struct dp_point_cfg *idu_process_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
