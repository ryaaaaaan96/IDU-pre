#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

enum { DEV = 7, DEV_ALT = 8 };
/* 两组模型故意使用相同 sig_id，验证键按 (device_id, sig_id) 隔离。 */
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
        .sig_flag = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
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

static const DM_SIG_CFG_S alt_rows[] = {
    {
        .sig_id = SIG_LOCAL, .sig_size = 2, .data_type = DATA_TYPE_U16,
        .data = {.def_value.u16 = 0x1111, .min_value.u16 = 0,
                 .max_value.u16 = UINT16_MAX},
    },
    {
        .sig_id = SIG_PARAM, .sig_size = 2,
        .sig_flag = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16,
        .data = {.def_value.u16 = 600, .min_value.u16 = 100,
                 .max_value.u16 = 1000},
    },
};

/* 业务层逐组登记，顺序不限。 */
static struct dp_point_state model_states[ARRAY_SIZE(rows)];
static struct dp_point_state alt_states[ARRAY_SIZE(alt_rows)];
static struct dp_table models[] = {
    {.device_id = DEV, .points = rows, .point_count = ARRAY_SIZE(rows),
     .states = model_states},
    {.device_id = DEV_ALT, .points = alt_rows,
     .point_count = ARRAY_SIZE(alt_rows), .states = alt_states},
};

/* 参数区挂载后再加入持久参数必须被拒绝。 */
static const DM_SIG_CFG_S late_rows[] = {
    {
        .sig_id = SIG_PARAM, .sig_size = 2, .sig_flag = FLAG_PERSISTENCE,
        .data_type = DATA_TYPE_U16,
        .data = {.def_value.u16 = 0, .min_value.u16 = 0,
                 .max_value.u16 = UINT16_MAX},
    },
};

static struct dp_point_state late_states[ARRAY_SIZE(late_rows)];
static struct dp_table late_model = {
    .device_id = 9,
    .points = late_rows,
    .point_count = ARRAY_SIZE(late_rows),
    .states = late_states,
};

/* 共用只读行，分别持有状态；数量超过旧目录上限 4。 */
static const DM_SIG_CFG_S array_row[] = {
    {.sig_id = 77, .sig_size = 3, .data_type = DATA_TYPE_U8ARRAY,
     .data.def_value.str = {1, 2, 3}},
};
static struct dp_point_state extra_states[5][ARRAY_SIZE(array_row)];
static struct dp_table extra_tables[] = {
    {.device_id = 20, .points = array_row, .point_count = ARRAY_SIZE(array_row),
     .states = extra_states[0]},
    {.device_id = 21, .points = array_row, .point_count = ARRAY_SIZE(array_row),
     .states = extra_states[1]},
    {.device_id = 22, .points = array_row, .point_count = ARRAY_SIZE(array_row),
     .states = extra_states[2]},
    {.device_id = 23, .points = array_row, .point_count = ARRAY_SIZE(array_row),
     .states = extra_states[3]},
    {.device_id = 24, .points = array_row, .point_count = ARRAY_SIZE(array_row),
     .states = extra_states[4]},
};

static void read_u16(uint16_t dev, uint16_t sig, uint16_t expected)
{
    uint8_t bytes[2] = {0};
    zassert_equal(dp_get_para(dev, sig, sizeof(bytes), bytes), sizeof(bytes),
                  "dev=%u sig=%u", dev, sig);
    zassert_equal(bytes[0] | ((uint16_t)bytes[1] << 8), expected,
                  "dev=%u sig=%u", dev, sig);
}

static void write_u16(uint16_t dev, uint16_t sig, uint16_t value, int expected)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    int err = dp_set_para(dev, sig, sizeof(bytes), bytes);
    zassert_equal(err, expected, "dev=%u sig=%u value=%u err=%d",
                  dev, sig, value, err);
}

ZTEST(dp_core, test_core)
{
    const char *phase = getenv("IDU_DP_PHASE");

    static struct dp_point_state duplicate_states[ARRAY_SIZE(alt_rows)];
    static struct dp_table duplicate = {
        .device_id = DEV, .points = alt_rows,
        .point_count = ARRAY_SIZE(alt_rows),
        .states = duplicate_states,
    };
    zassert_equal(dp_register_table(NULL), -EINVAL);
    zassert_ok(dp_register_table(&models[0]));
    zassert_equal(dp_register_table(&duplicate), -EEXIST);
    zassert_ok(dp_register_table(&models[1]));
    zassert_equal(dp_register_table(&models[1]), -EEXIST);

    struct dp_table no_states = {
        .device_id = 30, .points = array_row, .point_count = ARRAY_SIZE(array_row),
    };
    zassert_equal(dp_register_table(&no_states), -EINVAL);
    struct dp_table shared_states = {
        .device_id = 31, .points = rows, .point_count = ARRAY_SIZE(rows),
        .states = model_states,
    };
    zassert_equal(dp_register_table(&shared_states), -EINVAL);

    /* 表校验失败不得初始化一半，也不得挂入目录。 */
    static const DM_SIG_CFG_S bad_rows[] = {
        {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16},
        {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16},
    };
    static struct dp_point_state bad_states[ARRAY_SIZE(bad_rows)] = {
        {.value.words = {0x7777}},
    };
    static struct dp_table bad_table = {
        .device_id = 32, .points = bad_rows, .point_count = ARRAY_SIZE(bad_rows),
        .states = bad_states,
    };
    zassert_equal(dp_register_table(&bad_table), -EEXIST);
    zassert_equal(bad_states[0].value.words[0], 0x7777);
    uint8_t missing[2];
    zassert_equal(dp_get_para(32, 1, sizeof(missing), missing), -ENOENT);

    for (size_t i = 0; i < ARRAY_SIZE(extra_tables); ++i) {
        zassert_ok(dp_register_table(&extra_tables[i]));
        uint8_t bytes[4] = {0, 0, 0, 0xaa};
        zassert_equal(dp_get_para(extra_tables[i].device_id, 77, sizeof(bytes), bytes),
                      sizeof(bytes));
        const uint8_t expected[] = {1, 2, 3, 0xaa};
        zassert_mem_equal(bytes, expected, sizeof(bytes));
    }
    uint8_t array_value[] = {9, 8, 7};
    zassert_ok(dp_set_para(20, 77, sizeof(array_value), array_value));
    uint8_t array_result[3];
    zassert_equal(dp_get_para(20, 77, sizeof(array_result), array_result),
                  sizeof(array_result));
    zassert_mem_equal(array_value, array_result, sizeof(array_value));
    zassert_equal(extra_states[1][0].value.words[0], 0x0201);

    if (phase && !strcmp(phase, "verify")) {
        read_u16(DEV, SIG_PARAM, 700);
        read_u16(DEV_ALT, SIG_PARAM, 800);
        zassert_ok(dp_params_restore(DEV));
        read_u16(DEV, SIG_PARAM, 500);
        read_u16(DEV_ALT, SIG_PARAM, 800);
        zassert_ok(dp_params_restore(DEV_ALT));
        read_u16(DEV_ALT, SIG_PARAM, 600);
        return;
    }

    uint8_t constant[4] = {0};
    zassert_equal(dp_get_para(DEV, SIG_CONST, sizeof(constant), constant),
                  sizeof(constant));
    zassert_equal(constant[0], 0x78);
    zassert_equal(constant[1], 0x56);
    zassert_equal(constant[2], 0x34);
    zassert_equal(constant[3], 0x12);

    write_u16(DEV, SIG_LOCAL, 0x1234, 0);
    read_u16(DEV, SIG_LOCAL, 0x1234);
    write_u16(DEV, SIG_LOCAL, 0x1234, 0);
    zassert_equal(dp_set_para(DEV, SIG_LOCAL, 1, (uint8_t[]){0}), -EMSGSIZE);
    zassert_equal(dp_set_para(DEV, 99, 2, (uint8_t[]){0, 0}), -ENOENT);

    /* 同名的 sig_id 在另一组模型里是独立测点。 */
    read_u16(DEV_ALT, SIG_LOCAL, 0x1111);
    write_u16(DEV_ALT, SIG_LOCAL, 0x2222, 0);
    read_u16(DEV_ALT, SIG_LOCAL, 0x2222);
    read_u16(DEV, SIG_LOCAL, 0x1234);

    read_u16(DEV, SIG_PARAM, 500);
    write_u16(DEV, SIG_PARAM, 700, 0);
    read_u16(DEV, SIG_PARAM, 700);
    write_u16(DEV, SIG_PARAM, 2000, -ERANGE);
    zassert_equal(dp_set_para(DEV, SIG_CONST, 4, (uint8_t[]){1, 0, 0, 0}),
                  -EACCES);

    /* 参数按设备分别提交和恢复。 */
    read_u16(DEV_ALT, SIG_PARAM, 600);
    write_u16(DEV_ALT, SIG_PARAM, 800, 0);
    read_u16(DEV_ALT, SIG_PARAM, 800);
    read_u16(DEV, SIG_PARAM, 700);
    zassert_ok(dp_params_restore(DEV));
    read_u16(DEV, SIG_PARAM, 500);
    read_u16(DEV_ALT, SIG_PARAM, 800);
    zassert_ok(dp_params_restore(DEV_ALT));
    read_u16(DEV_ALT, SIG_PARAM, 600);
    write_u16(DEV, SIG_PARAM, 700, 0);
    write_u16(DEV_ALT, SIG_PARAM, 800, 0);

    write_u16(DEV, SIG_CMD, 0x1234, -EINVAL);
    write_u16(DEV, SIG_CMD, 0xa55a, 0);

    uint8_t state[8];
    zassert_equal(dp_get_para(DEV, SIG_STATUS, sizeof(state), state), sizeof(state));
    zassert_true(state[0] & DP_PARAM_STORAGE_READY);
    read_u16(DEV_ALT, SIG_PARAM, 800);
    zassert_equal(dp_register_table(&late_model), -EBUSY);

}

ZTEST_SUITE(dp_core, NULL, NULL, NULL, NULL, NULL);
