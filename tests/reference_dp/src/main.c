#include "../../fixtures/dp_tasks.h"
#include <data_process.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <errno.h>
#include <string.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_DP_PROCESS)
static int mode, calls, completions;
static uint16_t old_seen, new_seen;
static uint16_t addresses[2];
static size_t address_count;

static int32_t callback(FUNC_PARA_ALL *input, FUNC_PARA_ALL *output,
                        void *new_value, void *old_value)
{
    zassert_is_null(output);
    zassert_is_null(new_value);
    zassert_is_null(old_value);
    zassert_not_null(input);
    zassert_equal(input->para_num, 1);
    FUNC_PARA_S *item = input->para_all;
    ++calls;
    if (input->type == DP_PARA_GET) {
        zassert_equal(input->port_id, 7);
        zassert_equal(item->extra.reg_addr, 123);
        item->pvalue[0] = 0x56;
        item->pvalue[1] = 0x34;
        return mode == 4 ? 7 : mode == 5 ? -EINVAL : 0;
    }
    if (mode == 6) {
        zassert_true(address_count < ARRAY_SIZE(addresses));
        addresses[address_count++] = item->extra.reg_addr;
    }
    if (mode == 1) {
        uint8_t bytes[2];
        FUNC_PARA_S query = {.dev_id = 1, .sig_id = 2, .sig_size = 2, .pvalue = bytes};
        zassert_ok(dp_check_para_set(&query));
        zassert_ok(dp_get_para_old_value(&query));
        old_seen = bytes[0] | ((uint16_t)bytes[1] << 8);
        zassert_ok(dp_get_para_value(&query));
        new_seen = bytes[0] | ((uint16_t)bytes[1] << 8);
        uint8_t derived[4] = {0x78, 0x56, 0x34, 0x12};
        FUNC_PARA_S result = {.dev_id = 1, .sig_id = 3, .sig_size = 4, .pvalue = derived};
        zassert_ok(dp_set_para_value(&result));
        zassert_ok(dp_check_para_set(&result));
        zassert_ok(dp_set_para_flag(1, 2, 1U << PARA_NO_SET_DM));
        zassert_equal(dp_set_para_flag(1, 2, 0x80), -EINVAL);
        zassert_equal(dp_set_sig_value(1, 2, 2, bytes), -EDEADLK);
    } else if (mode == 2) {
        uint8_t bytes[4] = {1};
        zassert_ok(dp_set_para_only_dm_no_store(1, 3, 4, bytes));
        return -ECANCELED;
    } else if (mode == 3) {
        item->pvalue[0] = 0xaa;
        item->pvalue[1] = 0x55;
    }
    return mode == 4 ? 1 : 0;
}
static int native_read_error(struct dp_process_ctx *ctx, struct dp_key key)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(key);
    return 4;
}
#define CALLBACK .handler = dp_reference_process, .reference_handler = callback,
#else
#define CALLBACK
#endif

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U8,
     .width = 1, .minimum = 0, .maximum = 255, .initial.words = {42}},
    {.key = {1, 2}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .minimum = 0, .maximum = 65535, CALLBACK},
    {.key = {1, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U32,
     .width = 2, .minimum = 0, .maximum = UINT32_MAX},
    {.key = {1, 4}, .source = DP_SOURCE_LOCAL, .type = DP_WORDS,
     .width = 3, .size_bytes = 5, .reference_data_type = DATA_TYPE_STRING},
    {.key = {1, 5}, .source = DP_SOURCE_CONSTANT, .type = DP_U16,
     .width = 1, .minimum = 0, .maximum = 100, .initial.words = {17}},
    {.key = {1, 6}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .minimum = 0, .maximum = 65535, CALLBACK
#if defined(CONFIG_DP_PROCESS)
     .read_callback = true,
#endif
    },
    {.key = {1, 7}, .source = DP_SOURCE_LOCAL, .type = DP_WORDS,
     .width = DP_POINT_WORDS},
#if defined(CONFIG_DP_PROCESS)
    {.key = {1, 8}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
     .width = 1, .minimum = 0, .maximum = 65535,
     .handler = native_read_error, .read_callback = true},
#endif
};
static const struct dp_equip_cfg config = {.points = points, .point_count = ARRAY_SIZE(points)};

static void *setup(void)
{
    zassert_ok(dp_init_config(&config)); fixture_start_dp();
    return NULL;
}

ZTEST(reference_dp, test_direct_read_capacity_and_validation)
{
    uint8_t bytes[9];
    memset(bytes, 0xcc, sizeof(bytes));
    zassert_equal(dp_get_sig_value(1, 5, sizeof(bytes) - 1, bytes + 1), sizeof(bytes) - 1);
    zassert_equal(bytes[0], 0xcc);
    zassert_equal(bytes[1], 17);
    zassert_equal(bytes[2], 0);
    zassert_equal(bytes[3], 0xcc);
    zassert_equal(dp_get_sig_value(1, 5, 1, bytes), -EMSGSIZE);
    zassert_equal(dp_get_sig_value(1, 99, 2, bytes), -ENOENT);
    zassert_equal(dp_get_sig_value(1, 5, 2, NULL), -EINVAL);
    uint16_t type, size;
    zassert_ok(dp_get_sig_data_type(1, 1, &type, &size));
    zassert_equal(type, DATA_TYPE_U8);
    zassert_equal(size, 1);
    zassert_ok(dp_get_sig_data_type(1, 4, &type, &size));
    zassert_equal(type, DATA_TYPE_STRING);
    zassert_equal(size, 5);
}

#if defined(CONFIG_DP_PROCESS)
ZTEST(reference_dp, test_synchronous_bytes_and_bounds)
{
    mode = 0;
    uint8_t storage[] = {0xcc, 0xff, 0x12, 0x34, 0x56, 0xcc};
    zassert_ok(dp_set_sig_value(1, 1, 1, storage + 1));
    uint8_t bytes[DP_POINT_WORDS * 2 + 2];
    memset(bytes, 0xa5, sizeof(bytes));
    zassert_equal(dp_get_sig_value(1, 1, 3, bytes + 1), 3);
    zassert_equal(bytes[1], 255);
    zassert_equal(bytes[2], 0xa5);
    zassert_ok(dp_set_sig_value(1, 3, 4, storage + 1));
    zassert_equal(dp_get_sig_value(1, 3, 4, bytes + 1), 4);
    zassert_mem_equal(bytes + 1, storage + 1, 4);
    zassert_ok(dp_set_sig_value(1, 4, 5, storage));
    zassert_equal(dp_get_sig_value(1, 4, 7, bytes + 1), 7);
    zassert_mem_equal(bytes + 1, storage, 5);
    zassert_ok(dp_set_sig_value(1, 7, DP_POINT_WORDS * 2, bytes));
    zassert_equal(dp_set_sig_value(1, 1, 2, storage), -EMSGSIZE);
    zassert_equal(dp_set_sig_value(1, 3, 3, storage), -EMSGSIZE);
    zassert_equal(dp_set_sig_value(1, 3, 5, storage), -EMSGSIZE);
    zassert_equal(dp_set_sig_value(1, 5, 2, storage), -EACCES);
    zassert_equal(dp_set_sig_value(1, 99, 2, storage), -ENOENT);
}

ZTEST(reference_dp, test_remote_callback_helpers_and_flags)
{
    mode = 0;
    uint8_t old[] = {0x22, 0x11}, next[] = {0x44, 0x33}, result[4];
    zassert_ok(dp_set_sig_value(1, 2, 2, old));
    mode = 1;
    int before = calls;
    zassert_ok(dp_set_sig_value(1, 2, 2, next));
    zassert_equal(calls, before + 1);
    zassert_equal(old_seen, 0x1122);
    zassert_equal(new_seen, 0x3344);
    zassert_equal(dp_get_sig_value(1, 2, 2, result), 2);
    zassert_mem_equal(result, old, 2);
    zassert_equal(dp_get_sig_value(1, 3, 4, result), 4);
    zassert_equal(result[0], 0x78);
    zassert_equal(result[3], 0x12);
    FUNC_PARA_S query = {.dev_id = 1, .sig_id = 3, .sig_size = 4, .pvalue = result};
    zassert_equal(dp_get_para_value(&query), -EPERM);
    zassert_equal(dp_set_para_flag(1, 2, 0), -EPERM);
}

ZTEST(reference_dp, test_callback_failure_does_not_commit_outputs)
{
    mode = 0;
    uint8_t before[4] = {0x12, 0x34, 0x56, 0x78}, after[4], input[2] = {0};
    zassert_ok(dp_set_sig_value(1, 3, 4, before));
    mode = 2;
    zassert_equal(dp_set_sig_value(1, 2, 2, input), -ECANCELED);
    zassert_equal(dp_get_sig_value(1, 3, 4, after), 4);
    zassert_mem_equal(before, after, 4);
    mode = 3;
    zassert_ok(dp_set_sig_value(1, 2, 2, input));
    zassert_equal(dp_get_sig_value(1, 2, 2, after), 2);
    zassert_equal(after[0], 0xaa);
    zassert_equal(after[1], 0x55);
}

ZTEST(reference_dp, test_callback_return_conventions)
{
    mode = 4;
    uint8_t input[2] = {0x21, 0x43}, stored[2];
    zassert_ok(dp_set_sig_value(1, 2, 2, input));
    zassert_equal(dp_get_sig_value(1, 2, 2, stored), 2);
    zassert_mem_equal(stored, input, sizeof(input));
    DATA_SET_S read = {.dev_id = 1, .sig_id = 6, .sig_size = 2,
                      .extra.reg_addr = 123};
    PARA_DATA_S data = {.port_id = 7, .set_num = 1, .p_set_data = &read};
    zassert_equal(dp_fast_para_get(&data), 7);
    zassert_equal(read.p_value.str[0], 0x56);
    zassert_equal(read.p_value.str[1], 0x34);
    struct dp_point_state state;
    zassert_equal(dp_process_read_at((struct dp_key){1, 6}, 7, 3, 123, 1, &state), -EIO);
    mode = 5;
    memset(read.p_value.str, 0, sizeof(read.p_value.str));
    zassert_equal(dp_fast_para_get(&data), -EINVAL);
    zassert_equal(read.p_value.str[0], 0x56);
    zassert_equal(read.p_value.str[1], 0x34);
}

ZTEST(reference_dp, test_read_error_without_response_preserves_buffer)
{
    DATA_SET_S read = {.dev_id = 1, .sig_id = 8, .sig_size = 2,
                      .p_value.str = {0xa5, 0x5a}, .data_type = 0xcafe};
    PARA_DATA_S data = {.set_num = 1, .p_set_data = &read};
    zassert_equal(dp_fast_para_get(&data), 4);
    zassert_equal(read.p_value.str[0], 0xa5);
    zassert_equal(read.p_value.str[1], 0x5a);
    zassert_equal(read.data_type, 0xcafe);
}

static void completed(PARA_DATA_S *data)
{
    zassert_equal(data->set_num, 2);
    ++completions;
}

ZTEST(reference_dp, test_fast_batch_preserves_per_item_addresses)
{
    mode = 6;
    address_count = 0;
    DATA_SET_S items[] = {
        {.dev_id = 1, .sig_id = 2, .sig_size = 2, .extra.reg_addr = 5008,
         .p_value.str = {0x12, 0x34}},
        {.dev_id = 1, .sig_id = 6, .sig_size = 2, .extra.reg_addr = 31205,
         .p_value.str = {0x56, 0x78}},
    };
    PARA_DATA_S data = {.set_num = ARRAY_SIZE(items), .p_set_data = items};
    zassert_ok(dp_fast_para_set(&data, NULL));
    zassert_equal(address_count, 2);
    zassert_equal(addresses[0], 5008);
    zassert_equal(addresses[1], 31205);

    address_count = 0;
    struct dp_process_request request = {.operation = DP_OP_WRITE, .count = 1,
        .address = 5039, .items[0].point = {.key = {1, 2}, .width = 1,
                                           .value.words = {0x1234}}};
    struct dp_process_result result;
    zassert_ok(dp_process_execute(&request, &result));
    zassert_equal(address_count, 1);
    zassert_equal(addresses[0], 5039);
}

ZTEST(reference_dp, test_fast_batch_validation_and_read_callback)
{
    mode = 0;
    DATA_SET_S items[] = {
        {.dev_id = 1, .sig_id = 1, .sig_size = 1, .p_value.str = {0x67}},
        {.dev_id = 1, .sig_id = 3, .sig_size = 4, .p_value.str = {0x89, 0xab, 0xcd, 0xef}},
    };
    PARA_DATA_S data = {.set_num = 2, .p_set_data = items};
    int before = completions;
    zassert_ok(dp_fast_para_set(&data, completed));
    zassert_equal(completions, before + 1);
    uint8_t value;
    zassert_equal(dp_get_sig_value(1, 1, 1, &value), 1);
    zassert_equal(value, 0x67);
    items[0].p_value.str[0] = 0x12;
    items[1].sig_size = 3;
    zassert_equal(dp_fast_para_set(&data, completed), -EMSGSIZE);
    zassert_equal(completions, before + 1);
    zassert_equal(dp_get_sig_value(1, 1, 1, &value), 1);
    zassert_equal(value, 0x67);

    uint8_t stored[2] = {0x11, 0x22};
    zassert_ok(dp_set_sig_value(1, 6, 2, stored));
    DATA_SET_S read = {.dev_id = 1, .sig_id = 6, .sig_size = 2,
                      .extra.reg_addr = 123};
    data = (PARA_DATA_S){.port_id = 7, .set_num = 1, .p_set_data = &read};
    before = calls;
    zassert_equal(dp_fast_para_get(&data), 2);
    zassert_equal(calls, before + 1);
    zassert_equal(read.p_value.str[0], 0x56);
    zassert_equal(read.p_value.str[1], 0x34);
    zassert_equal(read.data_type, DATA_TYPE_U16);
    uint8_t direct[2];
    zassert_equal(dp_get_sig_value(1, 6, 2, direct), 2);
    zassert_equal(calls, before + 1);
    zassert_mem_equal(stored, direct, 2);
}

K_SEM_DEFINE(global_done, 0, 1);
static k_tid_t global_thread;
K_SEM_DEFINE(barrier_complete, 0, 1);
static void barrier_done(const struct dp_process_result *result, void *user)
{
    ARG_UNUSED(user);
    zassert_ok(result->error);
    k_sem_give(&barrier_complete);
}

static int32_t global_callback(FUNC_PARA_ALL *input, FUNC_PARA_ALL *output,
                               void *new_value, void *old_value)
{
    zassert_is_null(input);
    zassert_is_null(output);
    zassert_is_null(new_value);
    zassert_is_null(old_value);
    uint8_t bytes[] = {0x5a};
    int err = dp_set_para_only_dm(1, 1, 1, bytes);
    global_thread = k_current_get();
    k_sem_give(&global_done);
    return err ? err : 1;
}

ZTEST(reference_dp, test_global_transaction_and_native_submission)
{
    zassert_ok(dp_reference_invoke(global_callback, false));
    uint8_t byte;
    zassert_equal(dp_get_sig_value(1, 1, 1, &byte), 1);
    zassert_equal(byte, 0x5a);
    zassert_ok(dp_reference_invoke(global_callback, true));
    k_sem_reset(&global_done);
    zassert_ok(dp_reference_submit(global_callback, true));
    zassert_ok(k_sem_take(&global_done, K_SECONDS(1)));
    zassert_not_equal(global_thread, k_current_get());
    struct dp_process_request barrier = {.operation = DP_OP_READ, .count = 1,
                                         .items[0].point.key = {1, 5}};
    zassert_ok(dp_process_submit(&barrier, barrier_done, NULL));
    zassert_ok(k_sem_take(&barrier_complete, K_SECONDS(1)));
    uint16_t word = 0x37;
    uint32_t ticket = 0;
    zassert_ok(dp_submit_sig_value((struct dp_key){1, 1}, &word, 1, K_SECONDS(1), &ticket));
    zassert_true(ticket != 0);
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    zassert_equal(dp_get_sig_value(1, 1, 1, &byte), 1);
    zassert_equal(byte, 0x37);
}
#else
ZTEST(reference_dp, test_process_disabled_has_explicit_errors)
{
    uint8_t value = 1;
    zassert_equal(dp_set_sig_value(1, 1, 1, &value), -ENOTSUP);
    zassert_equal(dp_fast_para_set(NULL, NULL), -ENOTSUP);
    zassert_equal(dp_fast_para_get(NULL), -ENOTSUP);
    zassert_equal(dp_set_para_value(NULL), -ENOTSUP);
    zassert_equal(dp_reference_invoke(NULL, false), -ENOTSUP);
    zassert_equal(dp_get_sig_value(1, 1, 1, &value), 1);
    zassert_equal(value, 42);
}
#endif

static int isr_read, isr_write;
static void from_isr(const void *unused)
{
    ARG_UNUSED(unused);
    uint8_t byte = 1;
    isr_read = dp_get_sig_value(1, 1, 1, &byte);
    isr_write = dp_set_sig_value(1, 1, 1, &byte);
}

ZTEST(reference_dp, test_isr_boundaries)
{
    irq_offload(from_isr, NULL);
    zassert_equal(isr_read, -EWOULDBLOCK);
    zassert_equal(isr_write, IS_ENABLED(CONFIG_DP_PROCESS) ? -EWOULDBLOCK : -ENOTSUP);
}

ZTEST_SUITE(reference_dp, NULL, setup, NULL, NULL, NULL);
