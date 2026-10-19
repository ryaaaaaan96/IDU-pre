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

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100, .handler = linked},
    {.key = {1, 2}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 300},
    {.key = {1, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100, .handler = initialize_service},
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
    zassert_ok(dp_init(&equipment));
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
