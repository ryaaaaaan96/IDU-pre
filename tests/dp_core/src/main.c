#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

enum { DEV = 7 };
enum { SIG_LOCAL = 1, SIG_CONST, SIG_PARAM, SIG_CMD, SIG_STATUS };

static int command(struct dp_key key, const struct dp_value *value)
{
    ARG_UNUSED(key);
    return value->words[0] == 0xa55a ? 0 : -EINVAL;
}

static int status(struct dp_key key, struct dp_point_state *state)
{
    ARG_UNUSED(key);
    struct dp_param_status value;
    dp_params_get_status(&value);
    state->value.words[0] = value.flags;
    state->value.words[1] = (uint16_t)(int16_t)value.error;
    state->value.words[2] = value.sequence & 0xffff;
    state->value.words[3] = value.sequence >> 16;
    state->quality = DP_QUALITY_GOOD;
    return 0;
}

static const DM_SIG_CFG_S rows[] = {
    {
        .sig_id = SIG_LOCAL, .sig_size = 2, .data_type = DATA_TYPE_U16,
        .data = {.def_value.u16 = 0, .min_value.u16 = 0,
                 .max_value.u16 = UINT16_MAX},
    },
    {
        .sig_id = SIG_CONST, .sig_size = 4, .sig_flag = FLAG_CONSTANT,
        .data_type = DATA_TYPE_U32,
        .data = {.def_value.u32 = 0x12345678, .min_value.u32 = 0,
                 .max_value.u32 = UINT32_MAX},
    },
    {
        .sig_id = SIG_PARAM, .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .data = {.def_value.u16 = 500, .min_value.u16 = 100,
                 .max_value.u16 = 1000},
    },
    {
        .sig_id = SIG_CMD, .sig_size = 2, .data_type = DATA_TYPE_U16,
        .handler = command,
        .data = {.def_value.u16 = 0, .min_value.u16 = 0,
                 .max_value.u16 = UINT16_MAX},
    },
    {
        .sig_id = SIG_STATUS, .sig_size = 8, .sig_flag = FLAG_READ_CALLBACK,
        .data_type = DATA_TYPE_U16ARRAY, .read = status,
        .data = {.def_value.pst = NULL, .min_value.pst = NULL,
                 .max_value.pst = NULL},
    },
};

static const struct dp_model_cfg model = {
    .device_id = DEV,
    .points = rows,
    .point_count = ARRAY_SIZE(rows),
};

static void read_u16(uint16_t sig, uint16_t expected)
{
    uint8_t bytes[2] = {0};
    zassert_equal(dp_get_sig_value(DEV, sig, sizeof(bytes), bytes), sizeof(bytes));
    zassert_equal(bytes[0] | ((uint16_t)bytes[1] << 8), expected);
}

static void write_u16(uint16_t sig, uint16_t value, int expected)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    int err = dp_set_sig_value(DEV, sig, sizeof(bytes), bytes);
    zassert_equal(err, expected, "sig=%u value=%u err=%d", sig, value, err);
}

ZTEST(dp_core, test_core)
{
    const uint8_t area = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));
    const char *phase = getenv("IDU_DP_PHASE");
    zassert_ok(dp_start(&model, 1, area));

    if (phase && !strcmp(phase, "verify")) {
        read_u16(SIG_PARAM, 700);
        zassert_ok(dp_params_restore(DEV));
        read_u16(SIG_PARAM, 500);
        return;
    }

    uint8_t constant[4] = {0};
    zassert_equal(dp_get_sig_value(DEV, SIG_CONST, sizeof(constant), constant),
                  sizeof(constant));
    zassert_equal(constant[0], 0x78);
    zassert_equal(constant[1], 0x56);
    zassert_equal(constant[2], 0x34);
    zassert_equal(constant[3], 0x12);

    write_u16(SIG_LOCAL, 0x1234, 0);
    read_u16(SIG_LOCAL, 0x1234);
    write_u16(SIG_LOCAL, 0x1234, 0);
    zassert_equal(dp_set_sig_value(DEV, SIG_LOCAL, 1, (uint8_t[]){0}), -EMSGSIZE);
    zassert_equal(dp_set_sig_value(DEV, 99, 2, (uint8_t[]){0, 0}), -ENOENT);

    read_u16(SIG_PARAM, 500);
    write_u16(SIG_PARAM, 700, 0);
    read_u16(SIG_PARAM, 700);
    write_u16(SIG_PARAM, 2000, -ERANGE);
    zassert_equal(dp_set_sig_value(DEV, SIG_CONST, 4, (uint8_t[]){1, 0, 0, 0}),
                  -EACCES);

    write_u16(SIG_CMD, 0x1234, -EINVAL);
    write_u16(SIG_CMD, 0xa55a, 0);

    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){DEV, SIG_STATUS}, &state));
    zassert_true(state.value.words[0] & DP_PARAM_STORAGE_READY);

    zassert_ok(dp_params_restore(DEV));
    read_u16(SIG_PARAM, 500);
    write_u16(SIG_PARAM, 700, 0);
    dp_stop();
    zassert_equal(dp_set_sig_value(DEV, SIG_LOCAL, 2, (uint8_t[]){0, 0}),
                  -ECANCELED);
}

ZTEST_SUITE(dp_core, NULL, NULL, NULL, NULL, NULL);
