#include "../../fixtures/dp_tasks.h"
#include "../../fixtures/co_tasks.h"
#include "../../fixtures/tr_tasks.h"
#include "../../fixtures/point_connections.h"
#include <transmit/transmit.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>

static atomic_t sample_readback_enabled;
static int sample_readback(struct dp_process_ctx *ctx, struct dp_key key)
{
    ARG_UNUSED(key);
    if (ctx->request->operation != DP_OP_SAMPLE || !atomic_get(&sample_readback_enabled)) { return 0; }
    uint16_t word;
    int err = co_read(11, 4, 400, &word, 1);
    return err ? err : word == 4000 ? 0 : -EIO;
}

static const struct dp_point_cfg points[] = {
    {.key = {10, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1,
     .maximum = UINT16_MAX, .handler = sample_readback},
    {.key = {10, 2}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 3}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 4}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 5}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {11, 3}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 6}, .source = DP_SOURCE_MODBUS, .type = DP_WORDS, .width = 16},
    {.key = {10, 7}, .source = DP_SOURCE_MODBUS, .type = DP_WORDS, .width = 16},
#if defined(CONFIG_DP_PARAMS)
    {.key = {1, 10}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {25},
     .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST},
#endif
    {.key = {1, 11}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1, .maximum = 100},
    {.key = {1, 9}, .source = DP_SOURCE_CONSTANT, .type = DP_U16, .width = 1,
     .maximum = UINT16_MAX, .initial.words = {77}},
    {.key = {10, 8}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 10, .unit_id = 1, .bus_id = 0},
    {.dev_id = 11, .unit_id = 2, .bus_id = 0},
};
static const struct co_field first[] = {{.sig_id = 1, .width = 1}};
static const struct co_field second[] = {{.sig_id = 2, .width = 1}};
static const struct co_field third[] = {{.sig_id = 3, .width = 1}};
static const struct co_field fourth[] = {{.sig_id = 4, .width = 1}};
static const struct co_field fifth[] = {{.sig_id = 5, .width = 1}};
static const struct co_field sixth[] = {{.sig_id = 6, .width = 16}};
static const struct co_field seventh[] = {{.sig_id = 7, .width = 16}};
static const struct co_field eighth[] = {{.sig_id = 8, .width = 1}};
static const struct co_cfg polls[] = {
    {.dev_id = 10, .function = 4, .address = 10, .count = 1, .init_only = true,
     .fields = first, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 11, .count = 1, .init_only = true,
     .fields = second, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 100, .count = 1, .period_ms = 20,
     .fields = third, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 101, .count = 1, .period_ms = 20,
     .fields = fourth, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 103, .count = 1, .period_ms = 20,
     .fields = fifth, .field_count = 1},
    {.dev_id = 11, .function = 4, .address = 100, .count = 1, .period_ms = 20,
     .fields = third, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 200, .count = 16, .init_only = true,
     .fields = sixth, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 216, .count = 16, .init_only = true,
     .fields = seventh, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 232, .count = 1, .init_only = true,
     .fields = eighth, .field_count = 1},
};
static const struct dp_transmit_cfg maps[] = {
#if defined(CONFIG_DP_PARAMS)
    {.function = 3, .address = 3000, .count = 1, .key = {1, 10}},
#endif
    {.function = 4, .address = 1005, .count = 1, .key = {1, 11}},
    {.function = 4, .address = 1002, .count = 1, .key = {10, 2}},
    {.function = 4, .address = 1003, .count = 1, .key = {11, 3}},
    {.function = 4, .address = 1004, .count = 1, .key = {1, 9}},
    {.function = 4, .address = 2000, .count = 16, .key = {10, 6}},
    {.function = 4, .address = 1000, .count = 1, .key = {10, 3}},
    {.function = 4, .address = 1001, .count = 1, .key = {10, 4}},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = polls, .collect_count = ARRAY_SIZE(polls),
    .transmit = maps, .transmit_count = ARRAY_SIZE(maps),
};
struct request_event { uint16_t dev, address, count; int64_t at; };
K_MSGQ_DEFINE(requests, sizeof(struct request_event), 128, 4);
static atomic_t timeout_peer;
static atomic_t fault_device = ATOMIC_INIT(10);

static int transfer(void *user, uint16_t dev, struct modbus_adu *adu)
{
    ARG_UNUSED(user);
    uint16_t address = sys_get_be16(adu->data);
    uint16_t count = sys_get_be16(adu->data + 2);
    if (dev == atomic_get(&fault_device) && atomic_get(&timeout_peer) == 3) { return -EBUSY; }
    struct request_event event = {.dev = dev, .address = address, .count = count, .at = k_uptime_get()};
    zassert_ok(k_msgq_put(&requests, &event, K_NO_WAIT));
    if (dev == atomic_get(&fault_device) && atomic_get(&timeout_peer) == 1) { return -ETIMEDOUT; }
    if (dev == atomic_get(&fault_device) && (atomic_get(&timeout_peer) == 2 ||
        (atomic_get(&timeout_peer) == 5 && address >= 200))) {
        adu->fc |= 0x80; adu->length = 1; adu->data[0] = MODBUS_EXC_ILLEGAL_DATA_ADDR;
        return 0;
    }
    adu->data[0] = count * 2;
    adu->length = 1 + count * 2;
    for (size_t i = 0; i < count; ++i) { sys_put_be16((address + i) * 10, adu->data + 1 + i * 2); }
    if (dev == atomic_get(&fault_device) && atomic_get(&timeout_peer) == 4) { ++adu->data[0]; }
    return 0;
}

static int server_iface, client_iface;
static int north_request(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(server_iface, adu);
}
static int north_response(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(client_iface, adu);
}

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    struct dp_point_batch old = {.source = DP_SOURCE_MODBUS, .count = 1,
        .points = {{.key = {10, 2}, .width = 1, .value.words = {999}}}};
    zassert_ok(dp_publish(&old, K_NO_WAIT));
    k_msleep(2);
    zassert_ok(dp_invalidate_acquired_device(10, -ETIMEDOUT, k_uptime_get()));
    fixture_start_dp();
    zassert_ok(dp_wait_processed(old.ticket, K_SECONDS(1)));
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){10, 2}, &state));
    zassert_equal(state.quality & 0xff, DP_QUALITY_COMM_ERROR);
    zassert_not_equal(state.value.words[0], 999, "Queued stale samples cannot resurrect invalid data");
    k_msleep(1);
    fixture_connect_points();
#if defined(CONFIG_DP_PARAMS)
    zassert_equal(dp_params_init(&equipment, 255), -ENOENT, "Inject an unavailable parameter partition");
#endif
    server_iface = modbus_iface_get_by_name("RAW_0");
    client_iface = modbus_iface_get_by_name("RAW_1");
    struct modbus_iface_param server = {.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = tr_callbacks()},
        .rawcb = {.raw_tx_cb = north_response}};
    struct modbus_iface_param client = {.mode = MODBUS_MODE_RAW, .rx_timeout = 100000,
        .rawcb = {.raw_tx_cb = north_request}};
    zassert_ok(tr_attach(server_iface, NULL));
    fixture_bind_tr(server_iface);
    zassert_ok(tr_modbus_init_server(server_iface, server, NULL, true));
    zassert_ok(modbus_init_client(client_iface, client));
    const struct co_bus_cfg bus = {.id = 0, .iface = -1, .transfer = transfer};
    zassert_ok(co_init_bus(&equipment, &bus));
    static const struct co_device_policy policy = {.retries = 2, .exception_ms = 50, .probe_ms = 80};
    static const struct co_device_policy second_policy = {.retries = 0, .exception_ms = 30, .probe_ms = 40};
    zassert_ok(co_set_device_policy(10, &policy));
    zassert_ok(co_set_device_policy(11, &second_policy));
    return NULL;
}

ZTEST(collect_state, test_a_initialization_combines_contiguous_registers)
{
    zassert_ok(co_initialize_device(10));
    struct request_event event;
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.dev, 10);
    zassert_equal(event.address, 10);
    zassert_equal(event.count, 2);
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.address, 200);
    zassert_equal(event.count, 32);
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.address, 232);
    zassert_equal(event.count, 1);
    zassert_not_equal(k_msgq_get(&requests, &event, K_NO_WAIT), 0);
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){10, 2}, &state));
    zassert_equal(state.quality, DP_QUALITY_GOOD);
    zassert_equal(state.value.words[0], 110);
    zassert_ok(dp_read((struct dp_key){10, 7}, &state));
    zassert_equal(state.value.words[0], 2160);
    zassert_equal(state.value.words[15], 2310);
}

ZTEST(collect_state, test_b_periodic_groups_preserve_gaps_and_device_identity)
{
    zassert_ok(co_start());
    fixture_start_co();
    struct request_event event;
    bool combined = false, gap = false, other = false;
    for (unsigned i = 0; i < 3; ++i) {
        zassert_ok(k_msgq_get(&requests, &event, K_MSEC(50)));
        if (event.dev == 10 && event.address == 100) {
            zassert_equal(event.count, 2);
            combined = true;
        } else if (event.dev == 10 && event.address == 103) {
            zassert_equal(event.count, 1);
            gap = true;
        } else if (event.dev == 11 && event.address == 100) {
            zassert_equal(event.count, 1);
            other = true;
        } else { zassert_unreachable("Unexpected merged request"); }
    }
    zassert_true(combined && gap && other);
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, false)); }
    struct dp_point_state state;
    for (unsigned i = 0; i < 50; ++i) {
        zassert_ok(dp_read((struct dp_key){10, 4}, &state));
        if (state.quality == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(state.quality, DP_QUALITY_GOOD);
    zassert_equal(state.value.words[0], 1010);
}

static void wait_state(uint16_t dev, enum co_device_online wanted)
{
    for (unsigned i = 0; i < 500; ++i) {
        struct co_device_status status;
        zassert_ok(co_device_status(dev, &status));
        if (status.state == wanted) { return; }
        k_msleep(1);
    }
    zassert_unreachable("Device %u did not reach state %u", dev, wanted);
}

ZTEST(collect_state, test_c_explicit_timeout_does_not_change_device_state)
{
    int err = co_start();
    zassert_true(!err || err == -EALREADY);
    fixture_start_co();
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, true)); }
    wait_state(10, CO_DEVICE_ONLINE);
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, false)); }
    k_msgq_purge(&requests);
    atomic_set(&timeout_peer, 1);
    uint16_t value = 7;
    zassert_equal(co_write(10, 40000, &value, 1), -ETIMEDOUT);
    struct co_device_status status;
    zassert_ok(co_device_status(10, &status));
    zassert_equal(status.state, CO_DEVICE_ONLINE,
                  "An explicit write timeout must not drive the polling state machine");
    struct request_event event;
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.address, 40000);
    zassert_not_equal(k_msgq_get(&requests, &event, K_NO_WAIT), 0);
    atomic_clear(&timeout_peer);
}

static void enable_periodic(bool enabled)
{
    int err = co_start();
    zassert_true(!err || err == -EALREADY);
    fixture_start_co();
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, enabled)); }
}

ZTEST(collect_state, test_d_timeout_anomaly_offline_probe_and_reinitialization)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    k_msgq_purge(&requests);
    struct co_device_status other_before, other_after, status;
    zassert_ok(co_device_status(11, &other_before));
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_EXCEPTION);
    struct request_event event;
    unsigned attempts = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10 && event.address == 100) { ++attempts; }
    }
    zassert_equal(attempts, 3, "One send plus two configured retries");
    wait_state(10, CO_DEVICE_OFFLINE);
    zassert_ok(co_device_status(10, &status));
    zassert_false(status.online);
    struct dp_point_state cached;
    zassert_ok(dp_read((struct dp_key){10, 2}, &cached));
    zassert_equal(cached.value.words[0], 110, "Offline keeps the last initialization sample");
    zassert_equal(cached.quality & 0xff, DP_QUALITY_COMM_ERROR,
                  "Offline invalidates even initialization-only acquired points");
    zassert_ok(co_device_status(11, &other_after));
    zassert_true(other_after.successes > other_before.successes);
    k_msgq_purge(&requests);
    k_msleep(30);
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) { zassert_not_equal(event.dev, 10); }
    k_msleep(60);
    attempts = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10) {
            zassert_equal(event.address, 10, "Offline probes send only the first initialization packet");
            ++attempts;
        }
    }
    zassert_equal(attempts, 3);
    atomic_clear(&timeout_peer);
    wait_state(10, CO_DEVICE_ONLINE);
    unsigned init_first = 0, init_middle = 0, init_last = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev != 10) { continue; }
        init_first += event.address == 10;
        init_middle += event.address == 200;
        init_last += event.address == 232;
    }
    zassert_equal(init_first, 1);
    zassert_equal(init_middle, 1);
    zassert_equal(init_last, 1);
    enable_periodic(false);
}

ZTEST(collect_state, test_e_valid_exception_keeps_normal_communication)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    k_msgq_purge(&requests);
    atomic_set(&timeout_peer, 2);
    k_msleep(60);
    struct co_device_status status;
    zassert_ok(co_device_status(10, &status));
    zassert_equal(status.state, CO_DEVICE_ONLINE);
    zassert_true(status.online);
    zassert_equal(status.last_error, MODBUS_EXC_ILLEGAL_DATA_ADDR);
    struct request_event event;
    int64_t last = -100;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10 && event.address == 100) {
            zassert_true(event.at - last >= 20, "Valid exception responses must not cause retransmission");
            last = event.at;
        }
    }
    atomic_clear(&timeout_peer);
    enable_periodic(false);
}

ZTEST(collect_state, test_f_initialization_exception_stops_and_timeout_drops_directly)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_OFFLINE);
    k_msgq_purge(&requests);
    atomic_set(&timeout_peer, 2);
    wait_state(10, CO_DEVICE_INITIALIZING);
    k_msleep(60);
    struct co_device_status status;
    zassert_ok(co_device_status(10, &status));
    zassert_true(status.online);
    zassert_equal(status.state, CO_DEVICE_INITIALIZING);
    struct request_event event;
    unsigned attempts = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10) { zassert_equal(event.address, 10); ++attempts; }
    }
    zassert_equal(attempts, 1);
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_OFFLINE);
    int64_t last_send = -100;
    attempts = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10) { last_send = event.at; ++attempts; }
    }
    zassert_equal(attempts, 3);
    zassert_true(k_uptime_get() - last_send < 50,
                  "Initialization exhaustion must bypass the normal anomaly grace time");
    atomic_clear(&timeout_peer);
    wait_state(10, CO_DEVICE_ONLINE);
    enable_periodic(false);
}

ZTEST(collect_state, test_g_local_busy_does_not_count_as_missing_reply)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    k_msgq_purge(&requests);
    atomic_set(&timeout_peer, 3);
    k_msleep(90);
    struct co_device_status status;
    zassert_ok(co_device_status(10, &status));
    zassert_equal(status.state, CO_DEVICE_ONLINE);
    struct request_event event;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) { zassert_not_equal(event.dev, 10); }
    atomic_clear(&timeout_peer);
    enable_periodic(false);
}

ZTEST(collect_state, test_h_short_anomaly_recovery_does_not_repeat_initialization)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_EXCEPTION);
    atomic_clear(&timeout_peer);
    k_msgq_purge(&requests);
    wait_state(10, CO_DEVICE_ONLINE);
    struct request_event event;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 10) { zassert_true(event.address == 100 || event.address == 103); }
    }
    enable_periodic(false);
}

ZTEST(collect_state, test_i_no_initialization_group_recovers_on_valid_exception)
{
    enable_periodic(true);
    wait_state(11, CO_DEVICE_ONLINE);
    atomic_set(&fault_device, 11);
    atomic_set(&timeout_peer, 1);
    wait_state(11, CO_DEVICE_OFFLINE);
    k_msgq_purge(&requests);
    k_msleep(45);
    struct request_event event;
    unsigned attempts = 0;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        if (event.dev == 11) { zassert_equal(event.address, 100); ++attempts; }
    }
    zassert_equal(attempts, 1, "The second device has its own zero-retry policy");
    atomic_set(&timeout_peer, 2);
    wait_state(11, CO_DEVICE_ONLINE);
    struct co_device_status status;
    zassert_ok(co_device_status(11, &status));
    zassert_true(status.online);
    zassert_equal(status.last_error, MODBUS_EXC_ILLEGAL_DATA_ADDR);
    atomic_clear(&timeout_peer);
    atomic_set(&fault_device, 10);
    enable_periodic(false);
}

ZTEST(collect_state, test_j_malformed_payload_records_one_final_result)
{
    k_msgq_purge(&requests);
    struct co_device_status before, after;
    zassert_ok(co_device_status(10, &before));
    struct modbus_adu adu = {.fc = 4, .length = 4};
    sys_put_be16(100, adu.data); sys_put_be16(1, adu.data + 2);
    atomic_set(&timeout_peer, 4);
    zassert_equal(co_transfer(10, &adu), -EBADMSG);
    atomic_clear(&timeout_peer);
    zassert_ok(co_device_status(10, &after));
    zassert_equal(after.successes, before.successes);
    zassert_equal(after.failures, before.failures + 1);
}

ZTEST(collect_state, test_k_initialization_sample_can_read_back_without_lock_cycle)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_OFFLINE);
    k_msgq_purge(&requests);
    atomic_set(&sample_readback_enabled, 1);
    atomic_clear(&timeout_peer);
    wait_state(10, CO_DEVICE_ONLINE);
    struct request_event event;
    bool readback = false;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        readback |= event.dev == 11 && event.address == 400;
    }
    zassert_true(readback);
    atomic_clear(&sample_readback_enabled);
    enable_periodic(false);
}

ZTEST(collect_state, test_l_offline_north_exception_partial_recovery_and_diagnostic_clear)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    atomic_set(&timeout_peer, 1);
    wait_state(10, CO_DEVICE_OFFLINE);
    uint16_t value;
    zassert_equal(modbus_read_input_regs(client_iface, 16, 1002, &value, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1003, &value, 1));
    zassert_equal(value, 1000);
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1004, &value, 1));
    zassert_equal(value, 77);
    struct co_device_status status;
    zassert_ok(co_clear_device_errors(10));
    zassert_ok(co_device_status(10, &status));
    zassert_equal(status.state, CO_DEVICE_OFFLINE);
    zassert_false(status.online);
    zassert_equal(status.error, 0);
    zassert_equal(status.last_error, 0);
    zassert_equal(status.successes, 0);
    zassert_equal(status.failures, 0);
    zassert_equal(co_clear_device_errors(999), -ENOENT);
    struct dp_point_state cached;
    zassert_ok(dp_read((struct dp_key){10, 2}, &cached));
    zassert_equal(cached.quality & 0xff, DP_QUALITY_COMM_ERROR);
    zassert_equal(cached.value.words[0], 110);
    k_msgq_purge(&requests);
    k_msleep(20);
    struct request_event event;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        zassert_not_equal(event.dev, 10, "North reads and diagnostic clear must not trigger downstream reads");
    }
    atomic_set(&timeout_peer, 5); /* First initialization packet works, next packet has an exception. */
    for (unsigned i = 0; i < 150; ++i) {
        zassert_ok(dp_read((struct dp_key){10, 2}, &cached));
        if ((cached.quality & 0xff) == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(cached.quality & 0xff, DP_QUALITY_GOOD);
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1002, &value, 1));
    zassert_equal(value, 110);
    zassert_equal(modbus_read_input_regs(client_iface, 16, 2000, &value, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(dp_read((struct dp_key){10, 6}, &cached));
    zassert_equal(cached.value.words[0], 2000);
    zassert_not_equal(cached.quality & 0xff, DP_QUALITY_GOOD);
    atomic_clear(&timeout_peer);
    wait_state(10, CO_DEVICE_ONLINE);
    zassert_ok(co_device_status(10, &status));
    zassert_not_equal(status.last_error, 0, "Success retains historical errors");
    zassert_ok(modbus_read_input_regs(client_iface, 16, 2000, &value, 1));
    zassert_equal(value, 2000);
    enable_periodic(false);
}

ZTEST(collect_state, test_m_concurrent_diagnostic_clear_preserves_control_state)
{
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    atomic_set(&timeout_peer, 2);
    for (unsigned i = 0; i < 30; ++i) {
        zassert_ok(co_clear_device_errors(10));
        struct co_device_status status;
        zassert_ok(co_device_status(10, &status));
        zassert_equal(status.dev_id, 10);
        zassert_equal(status.state, CO_DEVICE_ONLINE);
        zassert_true(status.error == 0 || status.error == MODBUS_EXC_ILLEGAL_DATA_ADDR);
        k_msleep(1);
    }
    struct co_device_status other;
    zassert_ok(co_device_status(11, &other));
    zassert_true(other.successes > 0, "Clearing one device never clears another device's records");
    atomic_clear(&timeout_peer);
    enable_periodic(false);
}

ZTEST(collect_state, test_n_storage_failure_preserves_acquisition_and_rejects_persistent_write)
{
#if defined(CONFIG_DP_PARAMS)
    enable_periodic(true);
    wait_state(10, CO_DEVICE_ONLINE);
    struct dp_point_state ready;
    for (unsigned i = 0; i < 50; ++i) {
        zassert_ok(dp_read((struct dp_key){10, 3}, &ready));
        if ((ready.quality & 0xff) == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(ready.quality & 0xff, DP_QUALITY_GOOD);
    uint16_t local = 88, value;
    uint32_t ticket;
    zassert_ok(dp_submit_sig_value((struct dp_key){1, 11}, &local, 1, K_MSEC(20), &ticket));
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1005, &value, 1));
    zassert_equal(value, 88);
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1000, &value, 1));
    zassert_equal(value, 1000);
    zassert_ok(modbus_read_holding_regs(client_iface, 16, 3000, &value, 1));
    zassert_equal(value, 25);
    zassert_not_equal(modbus_write_holding_reg(client_iface, 16, 3000, 42), 0);
    zassert_ok(modbus_read_holding_regs(client_iface, 16, 3000, &value, 1));
    zassert_equal(value, 25);
    struct dp_point_update update = {.key = {1, 10}, .width = 1, .value.words = {42}};
    zassert_equal(dp_params_set_values(&update, 1), -EROFS);
    struct request_event event;
    while (!k_msgq_get(&requests, &event, K_NO_WAIT)) {
        zassert_not_equal(event.address, 400, "No downstream persistent side effect without storage");
    }
    zassert_equal(dp_params_prepare_reboot(), -EROFS, "The same failure must still block IAP commit");
    struct dp_param_status status;
    dp_params_get_status(&status);
    zassert_false(status.flags & DP_PARAM_STORAGE_READY);
    zassert_not_equal(status.error, 0);
    k_msleep(25);
    zassert_ok(modbus_read_input_regs(client_iface, 16, 1000, &value, 1));
    zassert_equal(value, 1000);
    enable_periodic(false);
#else
    ztest_test_skip();
#endif
}

ZTEST_SUITE(collect_state, NULL, setup, NULL, NULL, NULL);
