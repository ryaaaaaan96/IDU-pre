#include "../../fixtures/tr_tasks.h"
#include "../../fixtures/dp_tasks.h"
#include <collect/collect.h>
#include <transmit/transmit.h>
#include <dataprocess/process.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include "../../fixtures/point_connections.h"

BUILD_ASSERT(!IS_ENABLED(CONFIG_DP_PARAMS));
BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH));

static const struct dp_key input = {10, 1};
static const struct dp_key output = {1, 2};
static const struct dp_key dynamic = {1, 3};
static uint16_t remote_value = 12;
static unsigned read_count;
static unsigned timer_count;
static struct dp_process_result completion;
static K_SEM_DEFINE(done, 0, 1);
static K_SEM_DEFINE(timer_done, 0, 1);
/* Fault injection: hold the DP consumer to exercise completion waiters. */
static atomic_t hold_sample;
static K_SEM_DEFINE(sample_entered, 0, 1);
static K_SEM_DEFINE(sample_release, 0, 1);

static int scale(const struct dp_point_cfg *cfg, struct dp_value *value,
                 const struct dp_value *old)
{
    ARG_UNUSED(cfg);
    ARG_UNUSED(old);
    if (atomic_get(&hold_sample)) {
        k_sem_give(&sample_entered);
        k_sem_take(&sample_release, K_FOREVER);
    }
    value->words[0] *= 2;
    return 0;
}

static int linked(struct dp_process_ctx *ctx, struct dp_key key)
{
    ARG_UNUSED(key);
    struct dp_value value = {0};
    if (ctx->request->operation == DP_OP_INIT) {
        value.words[0] = 7;
    } else if (ctx->request->operation == DP_OP_TIMER) {
        value.words[0] = 90;
        ++timer_count;
        k_sem_give(&timer_done);
    } else {
        int err = dp_txn_get(ctx, input, false, &value);
        if (err) {
            return err;
        }
        value.words[0] += 3;
    }
    return dp_txn_emit(ctx, output, &value, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static int read_dynamic(struct dp_process_ctx *ctx, struct dp_key key)
{
    if (ctx->request->operation != DP_OP_READ) {
        return 0;
    }
    ++read_count;
    struct dp_value value = {.words = {333}};
    return dp_txn_emit(ctx, key, &value, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static const struct dp_point_cfg points[] = {
    {.key = {10, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .maximum = 1000, .process = scale, .handler = linked},
    {.key = {1, 2}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
     .width = 1, .maximum = 2000},
    {.key = {1, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
     .width = 1, .maximum = 2000, .handler = read_dynamic, .read_callback = true},
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 10, .unit_id = 9},
};
static const struct co_field fields[] = {
    {.sig_id = 1, .width = 1},
};
static const struct co_cfg polls[] = {
    {.dev_id = 10, .function = 4, .address = 20, .count = 1,
     .fields = fields, .field_count = ARRAY_SIZE(fields), .init_only = true},
};
static const struct dp_transmit_cfg maps[] = {
    {.function = 4, .address = 1000, .count = 1, .key = {10, 1}},
    {.function = 4, .address = 1001, .count = 1, .key = {1, 2}},
    {.function = 4, .address = 1002, .count = 1, .key = {1, 3}},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = polls, .collect_count = ARRAY_SIZE(polls),
    .transmit = maps, .transmit_count = ARRAY_SIZE(maps),
};

static int transfer(void *user, uint16_t dev_id, struct modbus_adu *adu,
                     k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    ARG_UNUSED(dev_id);
    ARG_UNUSED(user);
    zassert_equal(adu->unit_id, 9);
    if (adu->fc != 4 || adu->length != 4 ||
        sys_get_be16(adu->data) != 20 || sys_get_be16(adu->data + 2) != 1) {
        return -EINVAL;
    }
    adu->data[0] = 2;
    sys_put_be16(remote_value, adu->data + 1);
    adu->length = 3;
    return 0;
}

#if defined(CONFIG_TR_MODBUS_ADAPTER)
static int server_iface;
static int client_iface;

static int send_request(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(user);
    return modbus_raw_submit_rx(server_iface, adu);
}

static int send_response(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(user);
    return modbus_raw_submit_rx(client_iface, adu);
}
#endif

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    struct dp_point_batch cold = {.source = DP_SOURCE_MODBUS, .count = 1,
        .points = {{.key = {10, 1}, .width = 1, .value.words = {1}}}};
    zassert_ok(dp_publish(&cold, K_NO_WAIT));
    zassert_equal(dp_wait_processed(cold.ticket, K_MSEC(5)), -ETIMEDOUT,
                  "DP must wait until the application starts its consumers");
    fixture_start_dp();
    zassert_ok(dp_wait_processed(cold.ticket, K_SECONDS(1)));
    const struct co_bus_cfg bus = {
        .id = 0, .iface = -1, .simulated = true, .transfer = transfer,
    };
    zassert_ok(co_init_bus(&equipment, &bus));
    zassert_equal(co_poll(0), -ENOTCONN,
                  "Collection must not implicitly publish into DP");
    fixture_connect_points();
#if defined(CONFIG_TR_MODBUS_ADAPTER)
    server_iface = modbus_iface_get_by_name("RAW_0");
    client_iface = modbus_iface_get_by_name("RAW_1");
    zassert_true(server_iface >= 0 && client_iface >= 0);
    struct modbus_iface_param server = {
        .mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = tr_callbacks()},
        .rawcb = {.raw_tx_cb = send_response},
    };
    struct modbus_iface_param client = {
        .mode = MODBUS_MODE_RAW,
        .rx_timeout = 100000,
        .rawcb = {.raw_tx_cb = send_request},
    };
    zassert_ok(tr_attach(server_iface, NULL));
    fixture_bind_tr(server_iface);
    zassert_ok(tr_modbus_init_server(server_iface, server, NULL, true));
    zassert_ok(modbus_init_client(client_iface, client));
#endif
    return NULL;
}

static void completed(const struct dp_process_result *result, void *user)
{
    ARG_UNUSED(user);
    completion = *result;
    k_sem_give(&done);
}

static struct dp_point_state read_point(struct dp_key key)
{
    struct dp_point_state state;
    zassert_ok(dp_read(key, &state));
    return state;
}

struct sample_waiter {
    uint32_t ticket;
    int result;
    atomic_t finished;
};
static struct sample_waiter waiters[3];
static struct k_thread waiter_threads[3];
K_THREAD_STACK_ARRAY_DEFINE(waiter_stacks, 3, 2048);
static K_SEM_DEFINE(waiters_started, 0, 3);
static K_SEM_DEFINE(waiters_done, 0, 3);

static void wait_for_sample(void *arg, void *unused1, void *unused2)
{
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    struct sample_waiter *waiter = arg;
    k_sem_give(&waiters_started);
    waiter->result = dp_wait_processed(waiter->ticket, K_SECONDS(1));
    atomic_set(&waiter->finished, 1);
    k_sem_give(&waiters_done);
}

ZTEST(pipeline, test_sample_completion_broadcast_and_timeout)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_MODBUS, .count = 1,
        .points = {{.key = {10, 1}, .width = 1, .value.words = {17}}},
    };
    atomic_set(&hold_sample, 1);
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    uint32_t first = batch.ticket;
    zassert_ok(k_sem_take(&sample_entered, K_SECONDS(1)));
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    uint32_t second = batch.ticket;

    for (size_t i = 0; i < ARRAY_SIZE(waiters); ++i) {
        waiters[i].ticket = i < 2 ? first : second;
        waiters[i].result = -EINPROGRESS;
        atomic_clear(&waiters[i].finished);
        k_thread_create(&waiter_threads[i], waiter_stacks[i],
                        K_THREAD_STACK_SIZEOF(waiter_stacks[i]), wait_for_sample,
                        &waiters[i], NULL, NULL, 4, 0, K_NO_WAIT);
        zassert_ok(k_sem_take(&waiters_started, K_SECONDS(1)));
    }
    zassert_equal(dp_wait_processed(0, K_NO_WAIT), -EINVAL);
    zassert_equal(dp_wait_processed(first, K_NO_WAIT), -ETIMEDOUT);
    zassert_equal(dp_wait_processed(first, K_MSEC(5)), -ETIMEDOUT);

    k_sem_give(&sample_release);
    zassert_ok(k_sem_take(&sample_entered, K_SECONDS(1)));
    zassert_ok(k_sem_take(&waiters_done, K_SECONDS(1)));
    zassert_ok(k_sem_take(&waiters_done, K_SECONDS(1)));
    zassert_true(atomic_get(&waiters[0].finished));
    zassert_true(atomic_get(&waiters[1].finished));
    zassert_false(atomic_get(&waiters[2].finished));

    atomic_clear(&hold_sample);
    k_sem_give(&sample_release);
    zassert_ok(k_sem_take(&waiters_done, K_SECONDS(1)));
    for (size_t i = 0; i < ARRAY_SIZE(waiters); ++i) {
        zassert_ok(k_thread_join(&waiter_threads[i], K_SECONDS(1)));
        zassert_ok(waiters[i].result);
    }
    zassert_ok(dp_wait_processed(second, K_NO_WAIT));
    zassert_equal(read_point(input).value.words[0], 34);
}

ZTEST(pipeline, test_collect_initialization_without_storage)
{
    remote_value = 12;
    zassert_ok(co_initialize_device(10));
    struct dp_point_state state = read_point(input);
    zassert_equal(state.value.words[0], 24);
    zassert_equal(state.quality, DP_QUALITY_GOOD | DP_QUALITY_SIMULATED);
    zassert_equal(read_point(output).value.words[0], 7);
}

ZTEST(pipeline, test_samples_validate_and_run_handlers_without_storage)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_MODBUS, .flags = DP_QUALITY_SIMULATED, .count = 1,
        .points[0] = {.key = {10, 1}, .width = 1, .value.words = {20}},
    };
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    zassert_ok(dp_wait_processed(batch.ticket, K_SECONDS(1)));
    struct dp_point_state state = read_point(output);
    zassert_equal(state.value.words[0], 43);
    zassert_equal(state.quality, DP_QUALITY_GOOD | DP_QUALITY_SIMULATED);
    uint16_t words[2];
    zassert_ok(tr_read(4, 4, 1000, words, ARRAY_SIZE(words)));
    zassert_equal(words[0], 40);
    zassert_equal(words[1], 43);
#if defined(CONFIG_TR_MODBUS_ADAPTER)
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1000, words, ARRAY_SIZE(words)));
    zassert_equal(words[0], 40);
    zassert_equal(words[1], 43);
#endif
    batch.points[0].value.words[0] = 600;
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    zassert_ok(dp_wait_processed(batch.ticket, K_SECONDS(1)));
    zassert_equal(read_point(input).quality & 0xff, DP_QUALITY_BAD_VALUE);
    zassert_equal(read_point(output).value.words[0], 43);
}

ZTEST(pipeline, test_parameter_tables_require_storage_backend)
{
    const struct dp_point_cfg parameter = {
        .key = {1, 9}, .source = DP_SOURCE_PARAMETER,
        .type = DP_U16, .width = 1, .maximum = 100,
    };
    const struct dp_equip_cfg cfg = {.points = &parameter, .point_count = 1};
    zassert_equal(dp_validate(&cfg), -ENOTSUP);
}

ZTEST(pipeline, test_async_copy_timer_and_unavailable_persistence)
{
    struct dp_process_request request = {
        .id = 73, .operation = DP_OP_WRITE, .count = 1,
        .items[0].point = {.key = {10, 1}, .width = 1, .value.words = {50}},
    };
    zassert_ok(dp_process_submit(&request, completed, NULL));
    request.items[0].point.value.words[0] = 999;
    zassert_ok(k_sem_take(&done, K_SECONDS(1)));
    zassert_ok(completion.error);
    zassert_equal(completion.id, 73);
    zassert_equal(read_point(output).value.words[0], 53);
    timer_count = 0;
    int slot = dp_process_timer_start(input, 1, 0);
    zassert_true(slot >= 0);
    zassert_ok(k_sem_take(&timer_done, K_SECONDS(1)));
    k_sleep(K_MSEC(20));
    zassert_equal(timer_count, 1);
    zassert_equal(read_point(output).value.words[0], 90);
    zassert_ok(dp_process_timer_cancel(slot));
    zassert_equal(dp_process_save_reboot(100), -ENOTSUP);
}

ZTEST(pipeline, test_dynamic_read_and_modbus_without_storage)
{
    read_count = 0;
    struct dp_point_state state;
    zassert_ok(dp_process_read(dynamic, 4, &state));
    zassert_equal(state.value.words[0], 333);
    uint16_t words[1];
    zassert_ok(tr_read(4, 4, 1002, words, 1));
    zassert_equal(words[0], 333);
    zassert_equal(read_count, 2);
    zassert_equal(tr_write(4, 1002, words, 1), -ENOENT);
#if defined(CONFIG_TR_MODBUS_ADAPTER)
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1002, words, 1));
    zassert_equal(words[0], 333);
    zassert_equal(read_count, 3);
    zassert_true(modbus_read_input_regs(client_iface, 16, 1003, words, 1) != 0);
#else
    zassert_equal(tr_attach(0, NULL), -ENOTSUP);
    zassert_ok(tr_callbacks()->input_reg_rd(1002, words));
    zassert_equal(words[0], 333);
    zassert_equal(read_count, 3);
#endif
}

ZTEST_SUITE(pipeline, NULL, setup, NULL, NULL, NULL);
