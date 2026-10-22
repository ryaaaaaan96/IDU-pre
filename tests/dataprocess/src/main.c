#include "../../fixtures/dp_tasks.h"
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <zephyr/ztest.h>
#include <errno.h>
#if defined(CONFIG_DP_PARAMS)
#include <zephyr/storage/flash_map.h>
#endif

BUILD_ASSERT(!IS_ENABLED(CONFIG_MODBUS));
BUILD_ASSERT(!IS_ENABLED(CONFIG_COLLECT));
BUILD_ASSERT(!IS_ENABLED(CONFIG_TRANSMIT));

static const struct dp_key input = {1, 1};
static const struct dp_key output = {1, 2};
static const struct dp_key setter_input = {3, 1};
static const struct dp_key setter_output = {3, 2};
static K_SEM_DEFINE(completed, 0, 1);
static struct dp_process_result completion;
static bool fail_init;
static unsigned init_count, timer_count;
static uint16_t init_order[4];

static int linked(struct dp_process_ctx *ctx, struct dp_key key)
{
    if (ctx->request->operation == DP_OP_INIT) {
        init_order[init_count++] = key.sig_id;
    }
    if (ctx->request->operation == DP_OP_TIMER) {
        ++timer_count;
    }
    struct dp_value value;
    int err = dp_txn_get(ctx, key, false, &value);
    if (err) {
        return err;
    }
    value.words[0] *= 3;
    return dp_txn_emit(ctx, output, &value, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static int initialize_service(struct dp_process_ctx *ctx, struct dp_key key)
{
    if (ctx->request->operation == DP_OP_INIT) {
        init_order[init_count++] = key.sig_id;
        return fail_init ? -EIO : 0;
    }
    return 0;
}

static int increment_sample(struct dp_process_ctx *ctx, struct dp_key key)
{
    struct dp_value value;
    int err = dp_txn_get(ctx, key, false, &value);
    if (err) { return err; }
    uint32_t number = value.words[0] | ((uint32_t)value.words[1] << 16);
    ++number;
    value.words[0] = number;
    value.words[1] = number >> 16;
    return dp_txn_emit(ctx, setter_output, &value, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100, .handler = linked},
    {.key = {1, 2}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 300},
    {.key = {1, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100, .handler = initialize_service},
    {.key = {3, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U32, .width = 2,
     .maximum = UINT32_MAX, .handler = increment_sample},
    {.key = {3, 2}, .source = DP_SOURCE_LOCAL, .type = DP_U32, .width = 2,
     .maximum = UINT32_MAX},
    {.key = {3, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100},
    {.key = {3, 4}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100},
    {.key = {3, 5}, .source = DP_SOURCE_CONSTANT, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {17}},
    {.key = {3, 6}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1,
     .maximum = 100},
#if defined(CONFIG_DP_PARAMS)
    {.key = {1, 10}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {10}, .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST},
    {.key = {2, 11}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {50}, .write_address = 200,
     .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_FORWARD},
#endif
};
static const struct dp_device_cfg devices[] = {{.dev_id = 2, .unit_id = 9}};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
};

#if defined(CONFIG_DP_PARAMS)
static uint16_t device_value = 50;
static unsigned writes;

static int read_words(void *user, uint16_t dev_id, uint16_t address,
                      uint16_t *words, size_t count)
{
    zassert_equal(user, &device_value);
    zassert_equal(dev_id, 2);
    zassert_equal(address, 200);
    zassert_equal(count, 1);
    words[0] = device_value;
    return 0;
}

static int write_words(void *user, uint16_t dev_id, uint16_t address,
                       const uint16_t *words, size_t count)
{
    zassert_equal(user, &device_value);
    zassert_equal(dev_id, 2);
    zassert_equal(address, 200);
    zassert_equal(count, 1);
    device_value = words[0];
    ++writes;
    return 0;
}

static bool simulated(void *user, uint16_t dev_id)
{
    zassert_equal(user, &device_value);
    return dev_id == 2;
}

static const struct dp_params_transport transport = {
    .read = read_words, .write = write_words, .simulated = simulated, .user = &device_value,
};
#endif

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment)); fixture_start_dp();
#if defined(CONFIG_DP_PARAMS)
    const struct dp_params_transport invalid = {.read = read_words};
    zassert_equal(dp_params_set_transport(&invalid), -EINVAL);
    if (IS_ENABLED(CONFIG_DP_TEST_TRANSPORT)) {
        zassert_ok(dp_params_set_transport(&transport));
        zassert_ok(dp_params_set_transport(&transport));
    }
    zassert_ok(dp_params_init(&equipment, DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition))));
    if (IS_ENABLED(CONFIG_DP_TEST_TRANSPORT)) {
        zassert_ok(dp_params_set_transport(&transport));
        zassert_equal(dp_params_set_transport(NULL), -EBUSY);
    } else {
        zassert_ok(dp_params_set_transport(NULL));
        zassert_equal(dp_params_set_transport(&transport), -EBUSY);
    }
#endif
    return NULL;
}

static void on_complete(const struct dp_process_result *result, void *user)
{
    ARG_UNUSED(user);
    completion = *result;
    k_sem_give(&completed);
}

/* A later FIFO ticket also waits for calls that deliberately omit the ticket. */
static void setter_barrier(void)
{
    const uint16_t words[] = {0, 0};
    uint32_t ticket;
    zassert_ok(dp_submit_sig_value(setter_output, words, ARRAY_SIZE(words), K_MSEC(20), &ticket));
    zassert_not_equal(ticket, 0);
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
}

ZTEST(dataprocess, test_setter_copies_u32_words_and_runs_handler)
{
    uint16_t words[] = {0xffff, 0x1234};
    uint32_t ticket = 0;
    /* Keep the consumer from reading until the caller has reused its buffer. */
    k_sched_lock();
    int err = dp_submit_sig_value(setter_input, words, ARRAY_SIZE(words), K_NO_WAIT, &ticket);
    words[0] = words[1] = 0;
    k_sched_unlock();
    zassert_ok(err);
    zassert_not_equal(ticket, 0);
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    struct dp_point_state input_state, output_state;
    zassert_ok(dp_read(setter_input, &input_state));
    zassert_ok(dp_read(setter_output, &output_state));
    zassert_equal(input_state.value.words[0], 0xffff);
    zassert_equal(input_state.value.words[1], 0x1234);
    zassert_equal(output_state.value.words[0], 0);
    zassert_equal(output_state.value.words[1], 0x1235);
    zassert_equal(input_state.generation, output_state.generation);
    zassert_equal(input_state.quality, DP_QUALITY_GOOD);
    zassert_equal(output_state.quality, DP_QUALITY_GOOD);
    zassert_ok(input_state.error);
    zassert_ok(output_state.error);
}

ZTEST(dataprocess, test_setter_batch_generation_and_error_preserves_value)
{
    struct dp_point_update values[] = {
        {.key = {3, 3}, .width = 1, .value.words = {11}},
        {.key = {3, 4}, .width = 1, .value.words = {22}},
    };
    zassert_ok(dp_submit_sig_values(values, ARRAY_SIZE(values), K_MSEC(20), NULL));
    setter_barrier();
    struct dp_point_state before, first, second;
    zassert_ok(dp_read(values[1].key, &before));
    zassert_equal(before.value.words[0], 22);
    values[0].value.words[0] = 33;
    values[1].value.words[0] = 99;
    values[1].error = -ETIMEDOUT;
    uint32_t ticket = 0;
    k_sched_lock();
    int err = dp_submit_sig_values(values, ARRAY_SIZE(values), K_NO_WAIT, &ticket);
    values[0].value.words[0] = 0;
    values[1].error = 0;
    k_sched_unlock();
    zassert_ok(err);
    zassert_not_equal(ticket, 0);
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    zassert_ok(dp_read(values[0].key, &first));
    zassert_ok(dp_read(values[1].key, &second));
    zassert_equal(first.value.words[0], 33);
    zassert_equal(first.quality, DP_QUALITY_GOOD);
    zassert_ok(first.error);
    zassert_equal(second.value.words[0], 22);
    zassert_equal(second.quality, DP_QUALITY_COMM_ERROR);
    zassert_equal(second.error, -ETIMEDOUT);
    zassert_equal(first.generation, second.generation);
    zassert_equal(first.sampled_at, second.sampled_at);
    zassert_not_equal(second.generation, before.generation);
}

ZTEST(dataprocess, test_setter_rejects_invalid_batch_before_any_update)
{
    const struct dp_point_update baseline[] = {
        {.key = {3, 3}, .width = 1, .value.words = {11}},
        {.key = {3, 4}, .width = 1, .value.words = {22}},
    };
    uint32_t ticket;
    zassert_ok(dp_submit_sig_values(baseline, ARRAY_SIZE(baseline), K_MSEC(20), &ticket));
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    struct dp_point_state before;
    zassert_ok(dp_read(baseline[0].key, &before));
    const struct {
        struct dp_key key;
        uint8_t width;
        int error;
    } invalid[] = {
        {{3, 99}, 1, -ENOENT},
        {{3, 4}, 2, -EINVAL},
        {{3, 3}, 1, -EEXIST},
        {{3, 5}, 1, -EINVAL},
        {{3, 6}, 1, -EINVAL},
#if defined(CONFIG_DP_PARAMS)
        {{1, 10}, 1, -EINVAL},
#endif
    };
    for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i) {
        struct dp_point_update values[] = {
            {.key = {3, 3}, .width = 1, .value.words = {77}},
            {.key = invalid[i].key, .width = invalid[i].width, .value.words = {88}},
        };
        ticket = UINT32_MAX;
        zassert_equal(dp_submit_sig_values(values, ARRAY_SIZE(values), K_MSEC(20), &ticket),
                      invalid[i].error, "case %u", (unsigned)i);
        zassert_equal(ticket, 0);
        setter_barrier();
        struct dp_point_state state;
        zassert_ok(dp_read(baseline[0].key, &state));
        zassert_equal(state.value.words[0], before.value.words[0]);
        zassert_equal(state.generation, before.generation);
        zassert_equal(state.quality, DP_QUALITY_GOOD);
        zassert_ok(dp_read(baseline[1].key, &state));
        zassert_equal(state.value.words[0], 22);
        zassert_equal(state.generation, before.generation);
    }
}

ZTEST(dataprocess, test_setter_checks_input_sizes_before_copying)
{
    const uint16_t word = 1;
    const struct dp_point_update value = {.key = {3, 3}, .width = 1, .value.words = {1}};
    const size_t word_counts[] = {0, DP_POINT_WORDS + 1, SIZE_MAX};
    const size_t counts[] = {0, DP_BATCH_POINTS + 1, SIZE_MAX};
    uint32_t ticket = UINT32_MAX;
    zassert_equal(dp_submit_sig_value(value.key, NULL, 1, K_NO_WAIT, &ticket), -EINVAL);
    zassert_equal(ticket, 0);
    ticket = UINT32_MAX;
    zassert_equal(dp_submit_sig_values(NULL, 1, K_NO_WAIT, &ticket), -EINVAL);
    zassert_equal(ticket, 0);
    for (size_t i = 0; i < ARRAY_SIZE(word_counts); ++i) {
        ticket = UINT32_MAX;
        zassert_equal(dp_submit_sig_value(value.key, &word, word_counts[i], K_NO_WAIT, &ticket), -EINVAL);
        zassert_equal(ticket, 0);
    }
    for (size_t i = 0; i < ARRAY_SIZE(counts); ++i) {
        ticket = UINT32_MAX;
        zassert_equal(dp_submit_sig_values(&value, counts[i], K_NO_WAIT, &ticket), -EINVAL);
        zassert_equal(ticket, 0);
    }
    ticket = UINT32_MAX;
    zassert_equal(dp_submit_sig_value(setter_input, &word, 1, K_NO_WAIT, &ticket), -EINVAL);
    zassert_equal(ticket, 0);
    ticket = UINT32_MAX;
    zassert_equal(dp_submit_sig_value((struct dp_key){3, 99}, &word, 1, K_NO_WAIT, &ticket), -ENOENT);
    zassert_equal(ticket, 0);
}

ZTEST(dataprocess, test_setter_acceptance_does_not_hide_processing_error)
{
    const struct dp_key key = {3, 3};
    uint16_t word = 23;
    zassert_ok(dp_submit_sig_value(key, &word, 1, K_MSEC(20), NULL));
    setter_barrier();
    word = 101;
    uint32_t ticket = 0;
    zassert_ok(dp_submit_sig_value(key, &word, 1, K_MSEC(20), &ticket));
    zassert_not_equal(ticket, 0);
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    struct dp_point_state state;
    zassert_ok(dp_read(key, &state));
    zassert_equal(state.value.words[0], 23);
    zassert_equal(state.quality, DP_QUALITY_BAD_VALUE);
    zassert_equal(state.error, -ERANGE);
}

static K_SEM_DEFINE(setter_isr_done, 0, 1);
static int setter_isr_results[2];
static uint32_t setter_isr_tickets[2];

static void setter_from_isr(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    const struct dp_point_update value = {.key = {3, 3}, .width = 1, .value.words = {42}};
    setter_isr_results[0] = dp_submit_sig_value(value.key, value.value.words, 1,
                                           K_NO_WAIT, &setter_isr_tickets[0]);
    setter_isr_results[1] = dp_submit_sig_values(&value, 1, K_NO_WAIT, &setter_isr_tickets[1]);
    k_sem_give(&setter_isr_done);
}
static K_TIMER_DEFINE(setter_isr_timer, setter_from_isr, NULL);

ZTEST(dataprocess, test_setter_rejects_isr_calls)
{
    setter_isr_tickets[0] = setter_isr_tickets[1] = UINT32_MAX;
    k_timer_start(&setter_isr_timer, K_MSEC(1), K_NO_WAIT);
    zassert_ok(k_sem_take(&setter_isr_done, K_SECONDS(1)));
    zassert_equal(setter_isr_results[0], -EWOULDBLOCK);
    zassert_equal(setter_isr_results[1], -EWOULDBLOCK);
    zassert_equal(setter_isr_tickets[0], 0);
    zassert_equal(setter_isr_tickets[1], 0);
}

ZTEST(dataprocess, test_telemetry_and_async_without_protocol_modules)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_LOCAL, .count = 1,
        .points[0] = {.key = {1, 1}, .width = 1, .value.words = {7}},
    };
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    zassert_ok(dp_wait_processed(batch.ticket, K_SECONDS(1)));
    struct dp_point_state state;
    zassert_ok(dp_read(output, &state));
    zassert_equal(state.value.words[0], 21);
    struct dp_process_request request = {
        .id = 1, .operation = DP_OP_WRITE, .count = 1,
        .items[0].point = {.key = input, .width = 1, .value.words = {9}},
    };
    zassert_ok(dp_process_submit(&request, on_complete, NULL));
    zassert_ok(k_sem_take(&completed, K_SECONDS(1)));
    zassert_ok(completion.error);
    zassert_equal(completion.count, 2);
    zassert_ok(dp_read(output, &state));
    zassert_equal(state.value.words[0], 27);
}

ZTEST(dataprocess, test_parameter_backend_boundary)
{
#if defined(CONFIG_DP_PARAMS)
    struct dp_point_update local = {.key = {1, 10}, .width = 1, .value.words = {42}};
    zassert_ok(dp_params_set_values(&local, 1));
    zassert_ok(dp_params_reload());
    struct dp_point_state state;
    zassert_ok(dp_params_read(local.key, &state));
    zassert_equal(state.value.words[0], 42);
    struct dp_param_status before;
    dp_params_get_status(&before);
    struct dp_point_update remote = {.key = {2, 11}, .width = 1, .value.words = {55}};
    int err = dp_params_set_values(&remote, 1);
    if (IS_ENABLED(CONFIG_DP_TEST_TRANSPORT)) {
        zassert_ok(err);
        zassert_equal(device_value, 55);
        zassert_equal(writes, 1);
        zassert_ok(dp_params_reload());
        zassert_ok(dp_params_read(remote.key, &state));
        zassert_equal(state.value.words[0], 55);
        zassert_equal(state.quality, DP_QUALITY_GOOD | DP_QUALITY_SIMULATED);
    } else {
        zassert_equal(err, -ENODEV);
        struct dp_param_status after;
        dp_params_get_status(&after);
        zassert_equal(after.sequence, before.sequence);
        zassert_false(after.flags & DP_PARAM_PENDING);
        zassert_ok(dp_params_reload());
        zassert_ok(dp_params_read(remote.key, &state));
        zassert_equal(state.value.words[0], 50);
    }
#else
    zassert_equal(dp_process_save_reboot(100), -ENOTSUP);
#endif
}

ZTEST(dataprocess, test_declarative_lifecycle_rolls_back_timers_and_starts_once)
{
    const struct dp_key init_keys[] = {{1, 1}, {1, 3}};
    const struct dp_timer_spec timer = {
        .key = {1, 1}, .delay = K_MSEC(10), .period = K_NO_WAIT,
    };
    struct dp_process_plan plan = {
        .init_keys = init_keys, .init_count = ARRAY_SIZE(init_keys),
        .timers = &timer, .timer_count = 1,
    };
    /* Invalid plans cannot run INIT or reserve timers. */
    plan.initialize_all = true;
    zassert_equal(dp_process_start(&plan), -EINVAL);
    zassert_equal(init_count, 0);
    plan.initialize_all = false;
    struct dp_timer_spec invalid_timer = timer;
    invalid_timer.period = K_FOREVER;
    plan.timers = &invalid_timer;
    zassert_equal(dp_process_start(&plan), -EINVAL);
    zassert_equal(init_count, 0);
    plan.timers = &timer;
    int occupied[CONFIG_DP_PROCESS_TIMERS];
    for (size_t i = 0; i < ARRAY_SIZE(occupied); ++i) {
        occupied[i] = dp_process_timer_start(input, 1000, 0);
        zassert_true(occupied[i] >= 0);
    }
    zassert_equal(dp_process_start(&plan), -ENOSPC);
    zassert_equal(init_count, 0);
    for (size_t i = 0; i < ARRAY_SIZE(occupied); ++i) {
        zassert_ok(dp_process_timer_cancel(occupied[i]));
    }
    fail_init = true;
    zassert_equal(dp_process_start(&plan), -EIO);
    zassert_equal(init_count, 2);
    zassert_equal(init_order[0], 1);
    zassert_equal(init_order[1], 3);
    k_sleep(K_MSEC(50));
    zassert_equal(timer_count, 0);
    /* A failed initialization returns every reserved slot. */
    for (size_t i = 0; i < ARRAY_SIZE(occupied); ++i) {
        occupied[i] = dp_process_timer_start(input, 1000, 0);
        zassert_true(occupied[i] >= 0);
    }
    for (size_t i = 0; i < ARRAY_SIZE(occupied); ++i) {
        zassert_ok(dp_process_timer_cancel(occupied[i]));
    }
    fail_init = false;
    zassert_ok(dp_process_start(&plan));
    zassert_equal(init_count, 4);
    zassert_equal(dp_process_start(&plan), -EALREADY);
    k_sleep(K_MSEC(50));
    zassert_equal(timer_count, 1);
    k_sleep(K_MSEC(30));
    zassert_equal(timer_count, 1);
}

ZTEST_SUITE(dataprocess, NULL, setup, NULL, NULL, NULL);
