#include <dataprocess/points.h>
#include <dataprocess/params.h>
/* 私有数据只供本测试检查时间语义，不增加产品查询接口。 */
#include "dp_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

typedef enum { DEV = 7, DEV_ALT = 8 } test_device_t;
typedef enum { SIG_LOCAL = 1, SIG_CONST, SIG_PARAM, SIG_CMD, SIG_STATUS } test_signal_t;

static bool fail_allocation, fail_read;
void *__real_k_calloc(size_t count, size_t size);
void *__wrap_k_calloc(size_t count, size_t size)
{
    if (fail_allocation) { return NULL; }
    return __real_k_calloc(count, size);
}

static int fail_flash_write(const struct device *dev, off_t offset, uint8_t value)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(offset);
    ARG_UNUSED(value);
    return -EIO;
}

static int command(dp_key_t key, const dp_value_t *value)
{
    ARG_UNUSED(key);
    return value->u16 == 0xa55a ? 0 : -EINVAL;
}

static int status(dp_key_t key, dp_value_t *value)
{
    ARG_UNUSED(key);
    if (fail_read) { return -EIO; }
    dp_param_status_t status;
    dp_params_get_status(&status);
    value->words[0] = status.flags;
    value->words[1] = (uint16_t)(int16_t)status.error;
    value->words[2] = status.sequence & 0xffff;
    value->words[3] = status.sequence >> 16;
    return 0;
}

static const dp_point_def_t rows[] = {
    {.signal_id = SIG_LOCAL, .size = 2, .type = DP_TYPE_U16},
    {.signal_id = SIG_CONST, .size = 4, .type = DP_TYPE_U32, .flags = FLAG_CONSTANT,
     .data.initial.u32 = 0x12345678},
    {.signal_id = SIG_PARAM, .size = 2, .type = DP_TYPE_U16,
     .flags = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
     .data = {.initial.u16 = 500, .minimum.u16 = 100, .maximum.u16 = 1000}},
    {.signal_id = SIG_CMD, .size = 2, .type = DP_TYPE_U16, .write = command},
    {.signal_id = SIG_STATUS, .size = 8, .type = DP_TYPE_WORDS,
     .flags = FLAG_READ_CALLBACK, .read = status},
};
static const dp_point_def_t alt_rows[] = {
    {.signal_id = SIG_LOCAL, .size = 2, .type = DP_TYPE_U16, .data.initial.u16 = 0x1111},
    {.signal_id = SIG_PARAM, .size = 2, .type = DP_TYPE_U16,
     .flags = FLAG_PERSISTENCE | FLAG_RANGE_CHECK,
     .data = {.initial.u16 = 600, .minimum.u16 = 100, .maximum.u16 = 1000}},
};
static const dp_table_t local_table = {.points = rows, .point_count = ARRAY_SIZE(rows)};
static const dp_table_t alt_table = {.points = alt_rows, .point_count = ARRAY_SIZE(alt_rows)};
static const dp_point_def_t array_rows[] = {
    {.signal_id = 77, .size = 3, .type = DP_TYPE_BYTES, .data.initial.bytes = {1, 2, 3}},
};
static const dp_table_t array_table = {.points = array_rows, .point_count = ARRAY_SIZE(array_rows)};

static const dp_point_data_t *data(uint16_t device, uint16_t signal)
{
    for (const dp_registered_table_t *table = dp_tables(); table; table = table->next) {
        if (table->device_id != device) { continue; }
        for (size_t i = 0; i < table->point_count; ++i) {
            if (table->points[i].signal_id == signal) { return &table->data[i]; }
        }
    }
    return NULL;
}

static void read_u16(uint16_t device, uint16_t signal, uint16_t expected)
{
    uint8_t bytes[2];
    zassert_equal(dp_get_para(device, signal, sizeof(bytes), bytes), sizeof(bytes));
    zassert_equal(bytes[0] | ((uint16_t)bytes[1] << 8), expected);
}

static void write_u16(uint16_t device, uint16_t signal, uint16_t value, int expected)
{
    uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8)};
    zassert_equal(dp_set_para(device, signal, sizeof(bytes), bytes), expected);
}

ZTEST(dp_core, test_core)
{
    const char *phase = getenv("IDU_DP_PHASE");
    zassert_equal(dp_register_table(DEV, NULL), -EINVAL);
    zassert_equal(dp_register_table(0, &local_table), -EINVAL);
    const dp_table_t too_large = {.points = rows, .point_count = SIZE_MAX};
    zassert_equal(dp_register_table(DEV, &too_large), -EOVERFLOW);
    const dp_table_t empty = {0};
    zassert_equal(dp_register_table(DEV, &empty), -EINVAL);

    fail_allocation = true;
    zassert_equal(dp_register_table(DEV, &local_table), -ENOMEM);
    zassert_is_null(data(DEV, SIG_LOCAL));
    fail_allocation = false;
    zassert_ok(dp_register_table(DEV, &local_table));
    zassert_equal(dp_register_table(DEV, &alt_table), -EEXIST);
    zassert_ok(dp_register_table(DEV_ALT, &alt_table));
    zassert_equal(dp_register_table(DEV_ALT, &alt_table), -EEXIST);

    const dp_point_def_t bad_rows[] = {
        {.signal_id = 1, .size = 2, .type = DP_TYPE_U16},
        {.signal_id = 1, .size = 2, .type = DP_TYPE_U16},
    };
    const dp_table_t bad_table = {.points = bad_rows, .point_count = ARRAY_SIZE(bad_rows)};
    zassert_equal(dp_register_table(32, &bad_table), -EEXIST);
    zassert_is_null(data(32, 1));

    /* 同一份只读表登记给 5 个设备，目录及点数据均由模块持有。 */
    for (uint16_t device = 20; device < 25; ++device) {
        const dp_table_t temporary = array_table;
        zassert_ok(dp_register_table(device, &temporary));
        uint8_t bytes[] = {0, 0, 0, 0xaa};
        const uint8_t expected[] = {1, 2, 3, 0xaa};
        zassert_equal(dp_get_para(device, 77, sizeof(bytes), bytes), sizeof(bytes));
        zassert_mem_equal(bytes, expected, sizeof(bytes));
    }
    uint8_t array_value[] = {9, 8, 7};
    zassert_ok(dp_set_para(20, 77, sizeof(array_value), array_value));
    uint8_t array_result[3];
    zassert_equal(dp_get_para(20, 77, sizeof(array_result), array_result), sizeof(array_result));
    zassert_mem_equal(array_value, array_result, sizeof(array_value));
    zassert_equal(data(21, 77)->value.words[0], 0x0201);
    zassert_equal(data(20, 77)->updated_at_s, 0);

    /* 先确认未授时时不把 uptime 记作 UNIX 时间，再模拟上位机授时。 */
    const uint32_t epoch = 1761600000U;
    const struct timespec now = {.tv_sec = epoch};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &now));
    if (phase && !strcmp(phase, "verify")) {
        read_u16(DEV, SIG_PARAM, 700);
        read_u16(DEV_ALT, SIG_PARAM, 800);
        zassert_equal(data(DEV, SIG_PARAM)->updated_at_s, 0);
        zassert_ok(dp_params_restore(DEV));
        read_u16(DEV, SIG_PARAM, 500);
        read_u16(DEV_ALT, SIG_PARAM, 800);
        zassert_ok(dp_params_restore(DEV_ALT));
        read_u16(DEV_ALT, SIG_PARAM, 600);
        return;
    }

    uint8_t constant[4];
    zassert_equal(dp_get_para(DEV, SIG_CONST, sizeof(constant), constant), sizeof(constant));
    const uint8_t expected_constant[] = {0x78, 0x56, 0x34, 0x12};
    zassert_mem_equal(constant, expected_constant, sizeof(constant));
    zassert_equal(data(DEV, SIG_CONST)->value.u32, 0x12345678);
    zassert_equal(data(DEV, SIG_CONST)->updated_at_s, 0);
    write_u16(DEV, SIG_LOCAL, 0x1234, 0);
    read_u16(DEV, SIG_LOCAL, 0x1234);
    zassert_true(data(DEV, SIG_LOCAL)->updated_at_s >= epoch);
    zassert_true(data(DEV, SIG_LOCAL)->updated_at_s <= epoch + 2);
    const uint32_t last_update = data(DEV, SIG_LOCAL)->updated_at_s;
    const struct timespec later = {.tv_sec = epoch + 10};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &later));
    zassert_equal(dp_set_para(DEV, SIG_LOCAL, 1, (uint8_t[]){0}), -EMSGSIZE);
    zassert_equal(data(DEV, SIG_LOCAL)->updated_at_s, last_update);
    zassert_equal(dp_set_para(DEV, 99, 2, (uint8_t[]){0, 0}), -ENOENT);
    read_u16(DEV_ALT, SIG_LOCAL, 0x1111);
    write_u16(DEV_ALT, SIG_LOCAL, 0x2222, 0);
    read_u16(DEV, SIG_LOCAL, 0x1234);

    read_u16(DEV, SIG_PARAM, 500);
    zassert_equal(data(DEV, SIG_PARAM)->updated_at_s, 0);
    write_u16(DEV, SIG_PARAM, 700, 0);
    read_u16(DEV, SIG_PARAM, 700);
    zassert_true(data(DEV, SIG_PARAM)->updated_at_s >= epoch);
    const uint32_t parameter_update = data(DEV, SIG_PARAM)->updated_at_s;
    write_u16(DEV, SIG_PARAM, 2000, -ERANGE);
    read_u16(DEV, SIG_PARAM, 700);
    zassert_equal(data(DEV, SIG_PARAM)->updated_at_s, parameter_update);
    zassert_equal(dp_set_para(DEV, SIG_CONST, 4, (uint8_t[]){1, 0, 0, 0}), -EACCES);

    read_u16(DEV_ALT, SIG_PARAM, 600);
    write_u16(DEV_ALT, SIG_PARAM, 800, 0);
    zassert_ok(dp_params_restore(DEV));
    read_u16(DEV, SIG_PARAM, 500);
    read_u16(DEV_ALT, SIG_PARAM, 800);
    zassert_ok(dp_params_restore(DEV_ALT));
    read_u16(DEV_ALT, SIG_PARAM, 600);
    write_u16(DEV, SIG_PARAM, 700, 0);
    write_u16(DEV_ALT, SIG_PARAM, 800, 0);

    write_u16(DEV, SIG_CMD, 0x1234, -EINVAL);
    write_u16(DEV, SIG_CMD, 0xa55a, 0);
    read_u16(DEV, SIG_CMD, 0);
    zassert_equal(data(DEV, SIG_CMD)->updated_at_s, 0);
    uint8_t state[8];
    zassert_equal(dp_get_para(DEV, SIG_STATUS, sizeof(state), state), sizeof(state));
    zassert_true(state[0] & DP_PARAM_STORAGE_READY);
    const uint32_t read_update = data(DEV, SIG_STATUS)->updated_at_s;
    zassert_true(read_update >= epoch);
    const struct timespec after_read = {.tv_sec = epoch + 20};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &after_read));
    fail_read = true;
    zassert_equal(dp_get_para(DEV, SIG_STATUS, sizeof(state), state), -EIO);
    zassert_equal(data(DEV, SIG_STATUS)->updated_at_s, read_update);
    fail_read = false;

    const dp_point_def_t late_row = {.signal_id = SIG_PARAM, .size = 2,
                                    .type = DP_TYPE_U16, .flags = FLAG_PERSISTENCE};
    const dp_table_t late_table = {.points = &late_row, .point_count = 1};
    zassert_equal(dp_register_table(9, &late_table), -EBUSY);
    if (!phase) {
        /* Flash 提交失败不得覆盖共用缓存和更新时间。 */
        const dp_point_data_t before = *data(DEV, SIG_PARAM);
        const struct flash_area *area;
        zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(dp_params)), &area));
        const struct device *flash = flash_area_get_device(area);
        const struct flash_simulator_cb failure = {.write_byte = fail_flash_write};
        const struct flash_simulator_cb normal = {0};
        flash_simulator_set_callbacks(flash, &failure);
        write_u16(DEV, SIG_PARAM, 900, -EIO);
        flash_simulator_set_callbacks(flash, &normal);
        flash_area_close(area);
        zassert_equal(data(DEV, SIG_PARAM)->value.u16, before.value.u16);
        zassert_equal(data(DEV, SIG_PARAM)->updated_at_s, before.updated_at_s);
    }
    const struct timespec beyond_range = {.tv_sec = (int64_t)UINT32_MAX + 10};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &beyond_range));
    write_u16(DEV, SIG_LOCAL, 42, 0);
    zassert_equal(data(DEV, SIG_LOCAL)->updated_at_s, 0);
}

ZTEST_SUITE(dp_core, NULL, NULL, NULL, NULL, NULL);
