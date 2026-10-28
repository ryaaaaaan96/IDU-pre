#include <dataprocess/dataprocess.h>
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static bool fail_allocation, fail_callback;
static uint16_t write_result, read_result;

void *__real_k_calloc(size_t count, size_t size);

void *__wrap_k_calloc(size_t count, size_t size)
{
    return fail_allocation ? NULL : __real_k_calloc(count, size);
}

static int map_allocations_left = -1;
static size_t active_map_allocations;
void *__real_k_realloc(void *ptr, size_t size);

void *__wrap_k_realloc(void *ptr, size_t size)
{
    if (!size) {
        if (ptr) {
            --active_map_allocations;
        }
        return __real_k_realloc(ptr, size);
    }
    if (!map_allocations_left) {
        return NULL;
    }
    if (map_allocations_left > 0) {
        --map_allocations_left;
    }
    void *result = __real_k_realloc(ptr, size);
    if (result && !ptr) {
        ++active_map_allocations;
    }
    return result;
}

static int callback(const dp_callback_context_t *context)
{
    zassert_equal(context->device_id, 7);
    zassert_equal(context->signal_key, 300);
    zassert_equal(context->signal_index, 1);
    uint16_t replacement = context->is_write ? write_result : read_result;
    if (replacement) {
        context->value->u16 = replacement;
    }
    return fail_callback ? -EIO : 0;
}

static const dp_point_config_t rows[] = {
    {.signal_key = 99, .size = 3, .type = VALUE_TYPE_BYTES, .data.initial.bytes = {1, 2, 3}},
    {.signal_key = 300,
        .size = 2,
        .type = VALUE_TYPE_U16,
        .flags = DP_POINT_FLAG_CHECK_RANGE | DP_POINT_FLAG_SKIP_RESTORE,
        .write_callback = callback,
        .read_callback = callback,
        .data = {.initial.u16 = 50, .minimum.u16 = 1, .maximum.u16 = 100}},
    {.signal_key = 1,
        .size = 4,
        .type = VALUE_TYPE_U32,
        .flags = DP_POINT_FLAG_CHECK_RANGE,
        .data = {.initial.u32 = 0x12345678,
            .minimum.u32 = 0x10000000U,
            .maximum.u32 = 0xf0000000U}},
    {.signal_key = 2,
        .size = 2,
        .type = VALUE_TYPE_S16,
        .flags = DP_POINT_FLAG_CHECK_RANGE,
        .data = {.initial.s16 = -2, .minimum.s16 = -10, .maximum.s16 = 10}},
    {.signal_key = 3,
        .size = 4,
        .type = VALUE_TYPE_S32,
        .flags = DP_POINT_FLAG_CHECK_RANGE,
        .data = {.initial.s32 = -100000, .minimum.s32 = -200000, .maximum.s32 = 200000}},
    {.signal_key = 4,
        .size = 1,
        .type = VALUE_TYPE_U8,
        .flags = DP_POINT_FLAG_CHECK_RANGE,
        .data = {.initial.u8 = 2, .minimum.u8 = 1, .maximum.u8 = 9}},
    {.signal_key = 5, .size = 6, .type = VALUE_TYPE_WORDS, .data.initial.words = {1, 2, 3}},
};
static const dp_table_t table = {.points = rows, .point_count = ARRAY_SIZE(rows)};

static const dp_point_data_t *data(uint16_t device, size_t index)
{
    for (const dp_registered_table_t *item = dp_tables(); item; item = item->next) {
        if (item->device_id == device) {
            return &item->data[index];
        }
    }
    return NULL;
}

static void write_u16(uint16_t key, uint16_t value, int expected)
{
    const value_t input = {.u16 = value};
    zassert_equal(dp_set_para(7, key, &input), expected);
    zassert_equal(input.u16, value);
}

static void read_u16(uint16_t key, uint16_t expected)
{
    value_t value;
    zassert_ok(dp_get_para(7, key, &value));
    zassert_equal(value.u16, expected);
}

static K_THREAD_STACK_DEFINE(writer_stack, 2048);
static K_THREAD_STACK_DEFINE(reader_stack, 2048);
static struct k_thread writer_thread, reader_thread;

static void writer(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (size_t i = 0; i < 64; ++i) {
        uint8_t byte = i & 1 ? 0x55 : 0xaa;
        const value_t value = {.bytes = {byte, byte, byte}};
        zassert_ok(dp_set_para(7, 99, &value));
        k_yield();
    }
}

static void reader(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (size_t i = 0; i < 64; ++i) {
        value_t value;
        zassert_ok(dp_get_para(7, 99, &value));
        zassert_equal(value.bytes[0], value.bytes[1]);
        zassert_equal(value.bytes[1], value.bytes[2]);
        k_yield();
    }
}

ZTEST(dp_core, test_core)
{
    zassert_equal(dp_register_table(0, &table), -EINVAL);
    zassert_equal(dp_register_table(7, NULL), -EINVAL);
    const dp_table_t too_large = {.points = rows, .point_count = SIZE_MAX};
    zassert_equal(dp_register_table(7, &too_large), -EOVERFLOW);
    for (int allowed = 0; allowed < 2; ++allowed) {
        map_allocations_left = allowed;
        zassert_equal(dp_register_table(7, &table), -ENOMEM);
        zassert_is_null(data(7, 0));
        zassert_equal(active_map_allocations, 0);
    }
    map_allocations_left = -1;
    fail_allocation = true;
    zassert_equal(dp_register_table(7, &table), -ENOMEM);
    zassert_is_null(data(7, 0));
    fail_allocation = false;
    zassert_ok(dp_register_table(7, &table));
    zassert_equal(active_map_allocations, 0);
    zassert_equal(dp_register_table(7, &table), -EEXIST);
    zassert_ok(dp_register_table(8, &table));
    zassert_equal(data(7, 1)->value.u16, 50);
    zassert_equal(data(8, 1)->value.u16, 50);
    zassert_equal(data(7, 1)->updated_at_s, 0);

    static dp_point_config_t duplicates[512];
    for (size_t i = 0; i < ARRAY_SIZE(duplicates); ++i) {
        duplicates[i] =
            (dp_point_config_t){.signal_key = UINT16_MAX - i, .size = 2, .type = VALUE_TYPE_U16};
    }
    duplicates[511].signal_key = duplicates[0].signal_key;
    const dp_table_t duplicate_table = {
        .points = duplicates, .point_count = ARRAY_SIZE(duplicates)};
    for (size_t attempt = 0; attempt < 3; ++attempt) {
        zassert_equal(dp_register_table(9, &duplicate_table), -EEXIST);
        zassert_equal(active_map_allocations, 0);
    }
    zassert_is_null(data(9, 0));
    const dp_point_config_t invalid[] = {
        {.signal_key = 1, .size = 2, .type = VALUE_TYPE_U8},
        {.signal_key = 1, .size = 3, .type = VALUE_TYPE_WORDS},
        {.signal_key = 1, .size = 0, .type = VALUE_TYPE_U16},
        {.signal_key = 1,
            .size = 2,
            .type = VALUE_TYPE_U16,
            .flags = DP_POINT_FLAG_CHECK_RANGE,
            .data = {.minimum.u16 = 10, .maximum.u16 = 1}},
    };
    for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i) {
        const dp_table_t invalid_table = {.points = &invalid[i], .point_count = 1};
        zassert_equal(dp_register_table(9, &invalid_table), -EINVAL);
        zassert_is_null(data(9, 0));
    }
    value_t words;
    zassert_ok(dp_get_para(7, 5, &words));
    zassert_equal(words.words[0], 1);
    zassert_equal(words.words[1], 2);
    zassert_equal(words.words[2], 3);
    zassert_ok(dp_set_para(7, 4, &(value_t){.u8 = 9}));
    zassert_equal(dp_set_para(7, 4, &(value_t){.u8 = 10}), -ERANGE);
    write_u16(2, (uint16_t)-10, 0);
    write_u16(2, (uint16_t)-11, -ERANGE);
    zassert_ok(dp_set_para(7, 3, &(value_t){.s32 = -1}));
    zassert_equal(dp_set_para(7, 3, &(value_t){.s32 = INT32_MIN}), -ERANGE);
    zassert_equal(dp_set_para(7, 301, &(value_t){.u16 = 1}), -ENOENT);
    zassert_equal(dp_set_para(7, 300, NULL), -EINVAL);
    zassert_equal(dp_get_para(7, 300, NULL), -EINVAL);

    const uint32_t epoch = 1761600000U;
    const struct timespec now = {.tv_sec = epoch};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &now));
    write_result = 60;
    write_u16(300, 2000, 0);
    write_result = 0;
    read_u16(300, 60);
    zassert_equal(data(8, 1)->value.u16, 50);
    read_result = 70;
    read_u16(300, 70);
    read_result = 0;
    zassert_true(data(7, 1)->updated_at_s >= epoch);
    const dp_point_data_t before = *data(7, 1);
    write_result = 101;
    write_u16(300, 50, -ERANGE);
    write_result = 0;
    read_result = 101;
    value_t output = {.u16 = 0xbbaa};
    zassert_equal(dp_get_para(7, 300, &output), -ERANGE);
    zassert_equal(output.u16, 0xbbaa);
    read_result = 0;
    fail_callback = true;
    write_u16(300, 50, -EIO);
    zassert_equal(dp_get_para(7, 300, &output), -EIO);
    fail_callback = false;
    zassert_equal(data(7, 1)->value.u16, before.value.u16);
    zassert_equal(data(7, 1)->updated_at_s, before.updated_at_s);

    const value_t initial = {.bytes = {0x55, 0x55, 0x55}};
    zassert_ok(dp_set_para(7, 99, &initial));
    k_thread_create(&writer_thread,
        writer_stack,
        K_THREAD_STACK_SIZEOF(writer_stack),
        writer,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    k_thread_create(&reader_thread,
        reader_stack,
        K_THREAD_STACK_SIZEOF(reader_stack),
        reader,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_thread_join(&writer_thread, K_SECONDS(10)));
    zassert_ok(k_thread_join(&reader_thread, K_SECONDS(10)));
    write_u16(300, 80, 0);
    const dp_point_data_t skipped_before = *data(7, 1);
    const value_t other_device = {.bytes = {9, 8, 7}};
    zassert_ok(dp_set_para(8, 99, &other_device));
    zassert_ok(dp_set_para(7, 1, &(value_t){.u32 = 0xf0000000U}));
    zassert_equal(dp_set_para(7, 1, &(value_t){.u32 = 0xf0000001U}), -ERANGE);
    zassert_equal(dp_set_para(7, 1, &(value_t){.u32 = 0x0fffffffU}), -ERANGE);
    zassert_equal(dp_restore_defaults(0), -EINVAL);
    zassert_equal(dp_restore_defaults(123), -ENOENT);
    fail_callback = true;
    zassert_ok(dp_restore_defaults(7));
    fail_callback = false;
    const uint8_t default_bytes[] = {1, 2, 3};
    zassert_mem_equal(data(7, 0)->value.bytes, default_bytes, sizeof(default_bytes));
    zassert_equal(data(7, 2)->value.u32, 0x12345678);
    zassert_true(data(7, 2)->updated_at_s >= epoch);
    zassert_equal(data(7, 1)->value.u16, skipped_before.value.u16);
    zassert_equal(data(7, 1)->updated_at_s, skipped_before.updated_at_s);
    zassert_mem_equal(data(8, 0)->value.bytes, other_device.bytes, 3);

    const struct timespec beyond = {.tv_sec = (int64_t)UINT32_MAX + 1};
    zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &beyond));
    write_u16(300, 80, 0);
    zassert_equal(data(7, 1)->updated_at_s, 0);
}

ZTEST_SUITE(dp_core, NULL, NULL, NULL, NULL, NULL);
