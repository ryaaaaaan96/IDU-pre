#include <dataprocess/points.h>
#include <transmit/transmit.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

static int client, servers[2], target;
static atomic_t writes, coils, callbacks, filters, policies, replies;
static int filter_result, callback_result;
static bool callback_blocks, invalid_reply;
K_SEM_DEFINE(callback_entered, 0, 1);
K_SEM_DEFINE(callback_release, 0, 1);
K_SEM_DEFINE(transaction_done, 0, 1);
K_THREAD_STACK_DEFINE(transaction_stack, 2048);
static struct k_thread transaction_thread;
static int transaction_result;
static uint16_t transaction_value;

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_CONSTANT, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX, .initial.words = {2222}},
};
static const struct dp_transmit_cfg maps[] = {
    {.function = 4, .address = 100, .count = 1, .key = {1, 1}},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .transmit = maps, .transmit_count = ARRAY_SIZE(maps),
};

static int request_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(servers[target], adu);
}

static int response_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    atomic_inc(&replies);
    return modbus_raw_submit_rx(client, adu);
}

static int write_register(uint16_t address, const uint16_t *words, size_t count)
{
    ARG_UNUSED(address); ARG_UNUSED(words); ARG_UNUSED(count);
    atomic_inc(&writes);
    return 0;
}

static int write_coil(uint16_t address, bool state)
{
    ARG_UNUSED(address); ARG_UNUSED(state);
    atomic_inc(&coils);
    return 0;
}

static int mode_callback(int iface, const struct modbus_adu *rx,
                          struct modbus_adu *tx, void *user)
{
    zassert_equal(iface, servers[0]);
    zassert_equal(user, &equipment);
    atomic_inc(&callbacks);
    if (callback_blocks) {
        k_sem_give(&callback_entered);
        zassert_ok(k_sem_take(&callback_release, K_SECONDS(1)));
    }
    if (callback_result) { return callback_result; }
    *tx = *rx;
    tx->length = 3;
    tx->data[0] = 2;
    sys_put_be16(4321, tx->data + 1);
    if (invalid_reply) { tx->length = sizeof(tx->data) + 1; }
    return 0;
}

static int filter_callback(int iface, const struct modbus_adu *rx,
                            struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(rx); ARG_UNUSED(tx); ARG_UNUSED(user);
    atomic_inc(&filters);
    return filter_result;
}

static bool accept(const struct modbus_adu *rx, uint8_t unit, void *user)
{
    ARG_UNUSED(user);
    atomic_inc(&policies);
    return rx->unit_id == unit;
}

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    client = modbus_iface_get_by_name("RAW_0");
    servers[0] = modbus_iface_get_by_name("RAW_1");
    servers[1] = modbus_iface_get_by_name("RAW_2");
    zassert_true(client >= 0 && servers[0] >= 0 && servers[1] >= 0);
    static struct modbus_user_callbacks server_callbacks;
    server_callbacks = *tr_callbacks();
    server_callbacks.coil_wr = write_coil;
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        struct modbus_iface_param server = {
            .mode = MODBUS_MODE_RAW,
            .server = {.unit_id = 7 + i, .user_cb = &server_callbacks},
            .rawcb = {.raw_tx_cb = response_tx},
        };
        zassert_ok(tr_attach(servers[i], NULL));
        zassert_ok(tr_modbus_init_server(servers[i], server, write_register, true));
    }
    struct modbus_iface_param config = {
        .mode = MODBUS_MODE_RAW, .rx_timeout = 20000,
        .rawcb = {.raw_tx_cb = request_tx},
    };
    zassert_ok(modbus_init_client(client, config));
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        zassert_ok(tr_port_set_mode(servers[i], TR_MODE_SLAVE));
        zassert_ok(tr_port_set_handler(servers[i], TR_MODE_SLAVE, NULL, NULL));
        zassert_ok(tr_port_set_handler(servers[i], TR_MODE_CALLBACK, NULL, NULL));
        zassert_ok(tr_port_set_filter(servers[i], NULL, NULL));
        zassert_ok(tr_modbus_set_policy(servers[i], NULL));
        zassert_ok(tr_port_control(servers[i], TR_PORT_CLEAR_STATS, NULL));
    }
    target = 0;
    callback_result = -ENOENT;
    filter_result = -ENOENT;
    callback_blocks = false;
    invalid_reply = false;
    atomic_clear(&writes); atomic_clear(&coils); atomic_clear(&callbacks);
    atomic_clear(&filters); atomic_clear(&policies);
    atomic_clear(&replies);
    k_sem_reset(&callback_entered); k_sem_reset(&callback_release);
    k_sem_reset(&transaction_done);
}

ZTEST(transmit, test_reads_without_process_or_collect)
{
    uint16_t word;
    zassert_ok(tr_read_register(4, 100, &word));
    zassert_equal(word, 2222);
    zassert_ok(tr_read(0, 4, 100, &word, 1));
    zassert_equal(word, 2222);
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 2222);
    zassert_equal(tr_write(0, 100, &word, 1), -ENOTSUP);
    const struct tr_segment segment = {.begin = 100, .end = 100,
                                        .target_dev = 1, .target_begin = 0};
    const struct tr_routes routes = {.segments = &segment, .count = 1};
    struct modbus_adu rx = {.unit_id = 7, .fc = 4, .length = 4}, tx;
    sys_put_be16(100, rx.data); sys_put_be16(1, rx.data + 2);
    zassert_equal(tr_dispatch(servers[0], &rx, &tx, (void *)&routes), -ENOTSUP);
}

ZTEST(transmit, test_sleep_suppresses_all_handlers_and_preserves_other_port)
{
    const struct tr_modbus_policy policy = {.accept = accept};
    zassert_ok(tr_modbus_set_policy(servers[0], &policy));
    zassert_ok(tr_port_set_handler(servers[0], TR_MODE_SLAVE, mode_callback, (void *)&equipment));
    zassert_ok(tr_port_set_filter(servers[0], filter_callback, NULL));
    zassert_ok(tr_port_enable_filter(servers[0], true));
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_SLEEP));
    uint16_t word;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_write_holding_reg(client, 7, 100, 5), -ETIMEDOUT);
    zassert_equal(modbus_write_coil(client, 7, 1, true), -ETIMEDOUT);
    struct modbus_adu malformed = {.unit_id = 7, .fc = 16, .length = 2};
    zassert_ok(modbus_raw_submit_rx(servers[0], &malformed));
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&replies), 0);
    zassert_equal(atomic_get(&writes), 0);
    zassert_equal(atomic_get(&coils), 0);
    zassert_equal(atomic_get(&callbacks), 0);
    zassert_equal(atomic_get(&filters), 0);
    zassert_equal(atomic_get(&policies), 0);
    target = 1;
    zassert_ok(modbus_read_input_regs(client, 8, 100, &word, 1));
    zassert_equal(word, 2222);
    target = 0;
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_SLAVE));
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 2222);
    struct tr_port_stats stats;
    zassert_ok(tr_port_control(servers[0], TR_PORT_GET_STATS, &stats));
    zassert_equal(stats.requests, 1);
    zassert_equal(stats.responses, 1);
    zassert_equal(stats.sleep_drops, 4);
}

ZTEST(transmit, test_callback_mode_does_not_fall_through_to_writer)
{
    zassert_equal(tr_port_set_mode(servers[0], TR_MODE_CALLBACK), -ENOENT);
    zassert_ok(tr_port_set_handler(servers[0], TR_MODE_CALLBACK, mode_callback, (void *)&equipment));
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_CALLBACK));
    enum tr_port_mode mode;
    zassert_ok(tr_port_get_mode(servers[0], &mode));
    zassert_equal(mode, TR_MODE_CALLBACK);
    zassert_equal(tr_port_set_handler(servers[0], TR_MODE_CALLBACK, NULL, NULL), -EBUSY);
    zassert_equal(modbus_write_holding_reg(client, 7, 100, 5), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(atomic_get(&writes), 0);
    callback_result = 0;
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 4321);
    invalid_reply = true;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), MODBUS_EXC_SERVER_DEVICE_FAILURE);
    invalid_reply = false;
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_SLAVE));
    zassert_ok(modbus_write_holding_reg(client, 7, 100, 5));
    zassert_equal(atomic_get(&writes), 1);
}

ZTEST(transmit, test_filter_precedes_mode_and_routes)
{
    zassert_equal(tr_port_enable_filter(servers[0], true), -ENOENT);
    zassert_ok(tr_port_set_filter(servers[0], filter_callback, NULL));
    zassert_ok(tr_port_enable_filter(servers[0], true));
    zassert_ok(tr_port_set_handler(servers[0], TR_MODE_SLAVE, mode_callback, (void *)&equipment));
    filter_result = TR_MODBUS_SILENT;
    zassert_equal(modbus_write_holding_reg(client, 7, 100, 5), -ETIMEDOUT);
    zassert_equal(atomic_get(&writes), 0);
    zassert_equal(atomic_get(&callbacks), 0);
    filter_result = -EACCES;
    zassert_equal(modbus_write_holding_reg(client, 7, 100, 5), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    filter_result = -ENOENT;
    zassert_ok(modbus_write_holding_reg(client, 7, 100, 5));
    zassert_equal(atomic_get(&writes), 1);
    zassert_equal(atomic_get(&callbacks), 1);
    struct tr_port_stats stats;
    zassert_ok(tr_port_control(servers[0], TR_PORT_GET_STATS, &stats));
    zassert_equal(stats.filtered, 2);
    zassert_equal(stats.callback_errors, 1);
    zassert_ok(tr_port_enable_filter(servers[0], false));
    zassert_ok(modbus_write_holding_reg(client, 7, 100, 5));
    zassert_equal(atomic_get(&filters), 3);
}

static void transaction(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    transaction_result = modbus_read_input_regs(client, 7, 100, &transaction_value, 1);
    k_sem_give(&transaction_done);
}

ZTEST(transmit, test_sleep_allows_admitted_request_to_complete)
{
    callback_blocks = true;
    callback_result = 0;
    zassert_ok(tr_port_set_handler(servers[0], TR_MODE_CALLBACK, mode_callback, (void *)&equipment));
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_CALLBACK));
    k_thread_create(&transaction_thread, transaction_stack,
                    K_THREAD_STACK_SIZEOF(transaction_stack), transaction,
                    NULL, NULL, NULL, 8, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&callback_entered, K_MSEC(10)));
    zassert_ok(tr_port_set_mode(servers[0], TR_MODE_SLEEP));
    k_sem_give(&callback_release);
    zassert_ok(k_sem_take(&transaction_done, K_MSEC(30)));
    zassert_ok(k_thread_join(&transaction_thread, K_MSEC(30)));
    zassert_ok(transaction_result);
    zassert_equal(transaction_value, 4321);
    uint16_t word;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), -ETIMEDOUT);
    zassert_equal(atomic_get(&callbacks), 1);
}

ZTEST(transmit, test_control_validation_and_statistics_clear)
{
    zassert_equal(tr_port_set_mode(-1, TR_MODE_SLEEP), -EINVAL);
    zassert_equal(tr_port_set_mode(servers[0], (enum tr_port_mode)99), -EINVAL);
    zassert_equal(tr_port_get_mode(servers[0], NULL), -EINVAL);
    zassert_equal(tr_port_set_handler(servers[0], TR_MODE_SLEEP, mode_callback, NULL), -EINVAL);
    zassert_equal(tr_port_control(servers[0], TR_PORT_GET_STATS, NULL), -EINVAL);
    struct tr_port_stats stats;
    zassert_equal(tr_port_control(servers[0], TR_PORT_CLEAR_STATS, &stats), -EINVAL);
    zassert_ok(tr_port_control(servers[0], TR_PORT_CLEAR_STATS, NULL));
    zassert_ok(tr_port_control(servers[0], TR_PORT_GET_STATS, &stats));
    zassert_equal(stats.requests + stats.responses + stats.filtered +
                    stats.sleep_drops + stats.callback_errors, 0);
}

ZTEST_SUITE(transmit, NULL, setup, before, NULL, NULL);
