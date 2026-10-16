#include "idu_equip.h"
#include "idu_pre.h"
#include "idu_mvio.h"
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif
#include <errno.h>

static int current_process(const struct idu_point_cfg *cfg, struct idu_value *value,
                           const struct idu_value *old_value)
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
    {.key = {IDU_DEV_LOCAL, sig}, .source = IDU_SOURCE_CONSTANT, \
     .type = IDU_U16, .width = 1, .maximum = UINT16_MAX, .initial.words = {val}}
#define LOCAL_CURRENT(n) \
    {.key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n}, .source = IDU_SOURCE_LOCAL, \
     .type = IDU_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 1500, \
     .process = current_process}
#define REMOTE_CURRENT(n) \
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_CURRENT_1 + n}, .source = IDU_SOURCE_MODBUS, \
     .type = IDU_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 2000}

#define PARAM_FLAGS (IDU_PARAM_WRITE | IDU_PARAM_PERSIST | IDU_PARAM_RESTORE)

static const struct idu_point_cfg table[] = {
    {.key = {IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD}, .source = IDU_SOURCE_PARAMETER,
     .type = IDU_U16, .width = 1, .minimum = 100, .maximum = 1000,
     .initial.words = {CONFIG_IDU_SAMPLE_PERIOD_MS}, .param_flags = PARAM_FLAGS},
    {.key = {IDU_DEV_LOCAL, IDU_SIG_LABEL}, .source = IDU_SOURCE_PARAMETER,
     .type = IDU_WORDS, .width = 8, .initial.words = {0x4944, 0x552d, 0x7072, 0x6500},
     .param_flags = PARAM_FLAGS},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    /* Demo writable registers: replace only after the real slave map is known. */
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT}, .source = IDU_SOURCE_PARAMETER,
     .type = IDU_U16, .width = 1, .maximum = 1000, .initial.words = {100},
     .param_flags = PARAM_FLAGS | IDU_PARAM_FORWARD, .write_address = 40000},
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_OFFSET}, .source = IDU_SOURCE_PARAMETER,
     .type = IDU_S32, .width = 2, .minimum = -100000, .maximum = 100000,
     .param_flags = PARAM_FLAGS | IDU_PARAM_FORWARD, .write_address = 40001},
#endif
#if !defined(CONFIG_IDU_AIDC_PROFILE)
    {.key = {IDU_DEV_LOCAL, IDU_SIG_SAVED_ADDRESS}, .source = IDU_SOURCE_PARAMETER,
     .type = IDU_U16, .width = 1, .maximum = 246, .initial.words = {1},
     .param_flags = PARAM_FLAGS, .param_added = true,
     .seed_missing = idu_mvio_seed_address, .prepare_write = idu_mvio_prepare_address},
#endif
    CONST_U16(2503, 3),
    {.key = {IDU_DEV_LOCAL, 2504}, .source = IDU_SOURCE_CONSTANT,
     .type = IDU_WORDS, .width = 10,
     .initial.words = {0x5347, 0x2d46, 0x4143, 0x544f, 0x5259}}, /* SG-FACTORY */
    {.key = {IDU_DEV_LOCAL, 2581}, .source = IDU_SOURCE_CONSTANT,
     .type = IDU_WORDS, .width = 15,
     .initial.words = {0x4944, 0x552d, 0x7072, 0x655f, 0x302e, 0x312e, 0x3000}},
    {.key = {IDU_DEV_LOCAL, 2568}, .source = IDU_SOURCE_CONSTANT,
     .type = IDU_WORDS, .width = 4, .initial.words = {0x3031, 0x4152, 3, 0x0103}},
    CONST_U16(8149, IDU_DEVICE_TYPE),
    {.key = {IDU_DEV_LOCAL, IDU_SIG_PROTOCOL}, .source = IDU_SOURCE_CONSTANT,
     .type = IDU_U32, .width = 2, .maximum = UINT32_MAX,
     .initial.words = {IDU_PROTOCOL_VERSION & 0xffff, IDU_PROTOCOL_VERSION >> 16}},
    CONST_U16(5486, 0x1204),
    CONST_U16(IDU_SIG_VIRTUAL_MODE, IS_ENABLED(CONFIG_IDU_COLLECT_VIRTUAL)),
    {.key = {IDU_DEV_LOCAL, IDU_SIG_ADDRESS}, .source = IDU_SOURCE_LOCAL,
     .type = IDU_U16, .width = 1, .minimum = 1, .maximum = 16, .stale_ms = 1500},
    {.key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .source = IDU_SOURCE_LOCAL,
     .type = IDU_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 1500},
    LOCAL_CURRENT(0), LOCAL_CURRENT(1), LOCAL_CURRENT(2), LOCAL_CURRENT(3),
    LOCAL_CURRENT(4), LOCAL_CURRENT(5), LOCAL_CURRENT(6), LOCAL_CURRENT(7),

    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_DI}, .source = IDU_SOURCE_MODBUS,
     .type = IDU_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 2000},
    REMOTE_CURRENT(0), REMOTE_CURRENT(1), REMOTE_CURRENT(2), REMOTE_CURRENT(3),
    REMOTE_CURRENT(4), REMOTE_CURRENT(5), REMOTE_CURRENT(6), REMOTE_CURRENT(7),
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_PROTOCOL}, .source = IDU_SOURCE_MODBUS,
     .type = IDU_U32, .width = 2, .maximum = UINT32_MAX, .stale_ms = 15000},
    {.key = {IDU_DEV_REMOTE_IO, IDU_SIG_ADDRESS}, .source = IDU_SOURCE_MODBUS,
     .type = IDU_U16, .width = 1, .minimum = 1, .maximum = 247, .stale_ms = 15000},
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/points.inc"
#endif
};

const struct idu_point_cfg *idu_process_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
