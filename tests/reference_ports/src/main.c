#include "../../fixtures/dp_tasks.h"
#include "../../fixtures/point_connections.h"
#include <collect_lib.h>
#include <transmit_lib.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

static int client, servers[2], target;
static int filter_result, handler_result;
static atomic_t handler_calls, filter_calls, block_transfer, init_calls, writes;
K_SEM_DEFINE(transfer_entered, 0, 1);
K_SEM_DEFINE(transfer_release, 0, 1);
K_MSGQ_DEFINE(transfers, sizeof(uint8_t), 16, 1);
K_MSGQ_DEFINE(cancellations, sizeof(int), 4, 4);
K_THREAD_STACK_DEFINE(reader_stack, 2048);
static struct k_thread reader_thread;
static int reader_error;
static uint16_t south_value;
static int port_initializations, completions;

#define SAMPLE(dev, sig) { .key = {dev, sig}, .source = DP_SOURCE_MODBUS, \
                           .type = DP_U16, .width = 1, .maximum = UINT16_MAX }
static const struct dp_point_cfg points[] = {
    {.key = {10, 1}, .source = DP_SOURCE_CONSTANT, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX, .initial.words = {2222}},
    SAMPLE(1, 1), SAMPLE(1, 2), SAMPLE(1, 3), SAMPLE(2, 1), SAMPLE(2, 2),
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 1, .unit_id = 1}, {.dev_id = 2, .unit_id = 2},
};
static const struct co_field fields[] = {
    {.sig_id = 1, .width = 1}, {.sig_id = 2, .width = 1}, {.sig_id = 3, .width = 1},
};
static const struct co_cfg polls[] = {
    {.dev_id = 1, .function = 4, .address = 100, .count = 1, .period_ms = 100,
     .fields = &fields[0], .field_count = 1},
    {.dev_id = 1, .function = 4, .address = 100, .count = 1, .period_ms = 100,
     .fields = &fields[1], .field_count = 1},
    {.dev_id = 1, .function = 3, .address = 100, .count = 1, .period_ms = 100,
     .fields = &fields[2], .field_count = 1},
    {.dev_id = 2, .model_protocol = true, .model_id = 100, .address = 4,
     .count = 1, .init_only = true, .fields = &fields[0], .field_count = 1},
    {.dev_id = 2, .function = 4, .address = 100, .count = 1, .init_only = true,
     .fields = &fields[1], .field_count = 1},
};
static const struct dp_transmit_cfg maps[] = {
    {.function = 4, .address = 100, .count = 1, .key = {10, 1}},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = polls, .collect_count = ARRAY_SIZE(polls),
    .transmit = maps, .transmit_count = ARRAY_SIZE(maps),
};

static int transfer(void *user, uint16_t dev_id, struct modbus_adu *adu)
{
    ARG_UNUSED(dev_id);
    ARG_UNUSED(user);
    if (atomic_get(&block_transfer)) {
        k_sem_give(&transfer_entered);
        zassert_ok(k_sem_take(&transfer_release, K_SECONDS(1)));
    }
    zassert_ok(k_msgq_put(&transfers, &adu->fc, K_NO_WAIT));
    if (adu->fc == 6 || adu->fc == 16) {
        south_value = sys_get_be16(adu->data + (adu->fc == 6 ? 2 : 5));
        adu->length = 4;
        return 0;
    }
    adu->data[0] = 2;
    sys_put_be16(123, adu->data + 1);
    adu->length = 3;
    return 0;
}

static int initialize(void *user, uint16_t dev_id)
{
    ARG_UNUSED(user);
    zassert_equal(dev_id, 1);
    atomic_inc(&init_calls);
    return 0;
}

static int request_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(servers[target], adu);
}

static int response_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(client, adu);
}

static int handler(uint16_t iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    zassert_true(iface == servers[0] || iface == servers[1]);
    atomic_inc(&handler_calls);
    *tx = *rx;
    tx->length = 3;
    tx->data[0] = 2;
    sys_put_be16(4321, tx->data + 1);
    return handler_result;
}

static int filter(uint16_t iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    ARG_UNUSED(iface); ARG_UNUSED(rx); ARG_UNUSED(tx);
    atomic_inc(&filter_calls);
    return filter_result;
}

static int native_handler(int iface, const struct modbus_adu *rx,
                           struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(iface);
    zassert_equal(user, &equipment);
    *tx = *rx;
    tx->length = 3;
    tx->data[0] = 2;
    sys_put_be16(8765, tx->data + 1);
    return 0;
}

static int write_register(uint16_t address, const uint16_t *words, size_t count)
{
    ARG_UNUSED(address); ARG_UNUSED(words); ARG_UNUSED(count);
    atomic_inc(&writes);
    return 0;
}

static void init_server(size_t index)
{
    struct modbus_iface_param server = {
        .mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 7 + index, .user_cb = tr_callbacks()},
        .rawcb = {.raw_tx_cb = response_tx},
    };
    zassert_ok(tr_attach(servers[index], NULL));
    zassert_ok(tr_modbus_init_server(servers[index], server, write_register, true));
}

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment)); fixture_start_dp();
    const struct co_bus_cfg bus = {.id = 0, .iface = -1, .transfer = transfer,
                                   .initialize = initialize};
    fixture_connect_points();
    zassert_ok(co_init_bus(&equipment, &bus));
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, false)); }
    zassert_ok(co_start());
    client = modbus_iface_get_by_name("RAW_0");
    servers[0] = modbus_iface_get_by_name("RAW_1");
    servers[1] = modbus_iface_get_by_name("RAW_2");
    zassert_true(client >= 0 && servers[0] >= 0 && servers[1] >= 0);
    init_server(0);
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
    zassert_ok(co_set_port_mode(0, MODE_UPDATE, 100));
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, false)); }
    zassert_ok(co_cancel(0));
    k_msgq_purge(&transfers); k_msgq_purge(&cancellations);
    atomic_clear(&block_transfer); atomic_clear(&init_calls);
    k_sem_reset(&transfer_entered); k_sem_reset(&transfer_release);
    zassert_ok(co_set_port_mode(0, MODE_IDLE, 0));
    zassert_ok(co_init_func_reg(0, NULL));
    zassert_ok(co_set_value_trans_cb_reg(NULL));
    port_initializations = completions = 0;
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        zassert_ok(tr_set_port_mode(servers[i], MB_MODE_SLAVE));
        zassert_ok(tr_port_set_handler(servers[i], TR_MODE_SLAVE, NULL, NULL));
        zassert_ok(tr_port_set_handler(servers[i], TR_MODE_CALLBACK, NULL, NULL));
        zassert_ok(tr_reg_pkt_filter_func(servers[i], NULL));
        zassert_ok(tr_port_control(servers[i], TR_PORT_CLEAR_STATS, NULL));
        zassert_ok(tr_port_conn_status_config(servers[i], false));
    }
    zassert_ok(tr_reg_table_func(MB_MODE_SLAVE, NULL));
    zassert_ok(tr_reg_table_func(MB_MODE_CALLBACK, NULL));
    target = 0; handler_result = 0; filter_result = 0;
    atomic_clear(&handler_calls); atomic_clear(&filter_calls); atomic_clear(&writes);
}

ZTEST(reference_ports, test_byte_mode_buffers_and_numeric_boundaries)
{
    uint8_t bytes[] = {0xa5, 0xa5, 0xa5, 0xa5};
    zassert_ok(co_get_port_mode(0, bytes));
    zassert_equal(bytes[0], MODE_IDLE);
    zassert_mem_equal(bytes + 1, ((uint8_t[]){0xa5, 0xa5, 0xa5}), 3);
    zassert_equal(co_set_port_mode(0, MODE_OTHER, 0), -EINVAL);
    zassert_equal(co_set_port_mode(256, MODE_IDLE, 0), -EINVAL);
    zassert_ok(co_set_mode(0, CO_PORT_SLEEP));
    bytes[0] = 0xa5;
    zassert_equal(co_get_port_mode(0, bytes), -ENOTSUP);
    zassert_equal(bytes[0], 0xa5);
    zassert_ok(co_set_port_mode(0, MODE_IDLE, 0));
    zassert_ok(tr_set_port_mode(servers[0], MB_MODE_SLEEP));
    zassert_ok(tr_get_port_mode(servers[0], bytes));
    zassert_equal(bytes[0], MB_MODE_SLEEP);
    zassert_mem_equal(bytes + 1, ((uint8_t[]){0xa5, 0xa5, 0xa5}), 3);
    zassert_equal(tr_set_port_mode(servers[0], MB_MODE_MAX), -EINVAL);
    zassert_equal(tr_get_port_mode(UINT16_MAX, bytes), -EINVAL);
    zassert_equal(bytes[0], MB_MODE_SLEEP);
}

static void read_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    uint16_t word;
    reader_error = co_read(1, 4, 100, &word, 1);
}

static void release_transfer(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    k_sem_give(&transfer_release);
}
K_TIMER_DEFINE(release_timer, release_transfer, NULL);

ZTEST(reference_ports, test_collect_mode_timeout_waits_for_active_transfer)
{
    atomic_set(&block_transfer, 1);
    k_thread_create(&reader_thread, reader_stack, K_THREAD_STACK_SIZEOF(reader_stack),
                     read_thread, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&transfer_entered, K_MSEC(100)));
    zassert_equal(co_set_port_mode(0, MODE_UPDATE, 0), -EBUSY);
    int64_t start = k_uptime_get();
    zassert_equal(co_set_port_mode(0, MODE_UPDATE, 10), -ETIMEDOUT);
    zassert_true(k_uptime_get() - start >= 10);
    k_timer_start(&release_timer, K_MSEC(30), K_NO_WAIT);
    zassert_ok(co_set_port_mode(0, MODE_UPDATE, 100));
    zassert_ok(k_thread_join(&reader_thread, K_MSEC(100)));
    zassert_ok(reader_error);
    uint8_t mode;
    zassert_ok(co_get_port_mode(0, &mode));
    zassert_equal(mode, MODE_UPDATE);
}

ZTEST(reference_ports, test_scan_selector_changes_all_matching_blocks)
{
    uint8_t fc;
    zassert_ok(co_set_scan_pkg_flag(1, 100, 4, CO_SCAN_PKG_ON));
    for (size_t i = 0; i < 2; ++i) {
        zassert_ok(k_msgq_get(&transfers, &fc, K_MSEC(50)));
        zassert_equal(fc, 4);
    }
    zassert_ok(co_set_port_mode(0, MODE_UPDATE, 100));
    zassert_ok(co_set_scan_pkg_flag(1, 100, 4, CO_SCAN_PKG_OFF));
    k_msgq_purge(&transfers);
    zassert_ok(co_set_port_mode(0, MODE_IDLE, 100));
    zassert_not_equal(k_msgq_get(&transfers, &fc, K_MSEC(120)), 0);
    zassert_equal(co_set_scan_pkg_flag(1, 101, 4, CO_SCAN_PKG_ON), -ENOENT);
    zassert_equal(co_set_scan_pkg_flag(1, 100, 256 + 4, CO_SCAN_PKG_ON), -EINVAL);
    zassert_equal(co_set_scan_pkg_flag(1, 100, 4, 2), -EINVAL);
    zassert_equal(co_set_scan_pkg_flag(2, 100, 4, CO_SCAN_PKG_ON), -EPROTOTYPE);
}

static void canceled(const struct co_request *request, int error, void *user)
{
    zassert_equal(request->id, 99);
    zassert_equal(user, &equipment);
    zassert_ok(k_msgq_put(&cancellations, &error, K_NO_WAIT));
}

ZTEST(reference_ports, test_cancel_and_device_initialization)
{
    zassert_ok(co_set_port_mode(0, MODE_UPDATE, 100));
    struct co_request request = {.id = 99, .dev_id = 1, .mode = CO_PORT_POLL,
                                  .adu = {.fc = 6, .length = 4}};
    zassert_ok(co_submit(&request, canceled, (void *)&equipment));
    CO_CLEAN_QUEUE_ST queue = {.port_id = 0};
    zassert_ok(co_clean_set_queue(&queue));
    int error;
    zassert_ok(k_msgq_get(&cancellations, &error, K_NO_WAIT));
    zassert_equal(error, -ECANCELED);
    zassert_ok(co_clean_set_queue(&queue));
    zassert_equal(k_msgq_num_used_get(&cancellations), 0);
    queue.port_id = 256;
    zassert_equal(co_clean_set_queue(&queue), -EINVAL);
    zassert_ok(co_dev_init(1));
    zassert_equal(atomic_get(&init_calls), 1);
    uint8_t mode;
    zassert_ok(co_get_port_mode(0, &mode));
    zassert_equal(mode, MODE_UPDATE);
}

ZTEST(reference_ports, test_global_handlers_apply_to_existing_and_future_ports)
{
    zassert_ok(tr_reg_table_func(MB_MODE_SLAVE, handler));
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 4321);
    init_server(1); /* Global registration predates this interface. */
    target = 1;
    zassert_ok(modbus_read_input_regs(client, 8, 100, &word, 1));
    zassert_equal(word, 4321);
    zassert_equal(atomic_get(&handler_calls), 2);
    zassert_ok(tr_port_set_handler(servers[1], TR_MODE_SLAVE, native_handler, (void *)&equipment));
    zassert_ok(modbus_read_input_regs(client, 8, 100, &word, 1));
    zassert_equal(word, 8765);
    zassert_equal(atomic_get(&handler_calls), 2);
}

ZTEST(reference_ports, test_handler_and_filter_return_conventions)
{
    zassert_ok(tr_reg_table_func(MB_MODE_SLAVE, handler));
    zassert_ok(tr_reg_pkt_filter_func(servers[0], filter));
    zassert_ok(tr_pkt_filter_config(servers[0], true));
    uint16_t word;
    handler_result = -1;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 2222); /* Mode -1 and filter 0 both continue. */
    handler_result = 0;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 4321); /* Mode 0 replies; filter 0 does not reply. */
    filter_result = 4;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), -ETIMEDOUT);
    zassert_equal(atomic_get(&handler_calls), 2);
    filter_result = 2;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(atomic_get(&handler_calls), 2);
    zassert_equal(atomic_get(&filter_calls), 4);
}

ZTEST(reference_ports, test_callback_global_lifetime_and_no_write_fallthrough)
{
    zassert_equal(tr_set_port_mode(servers[0], MB_MODE_CALLBACK), -ENOENT);
    zassert_ok(tr_reg_table_func(MB_MODE_CALLBACK, handler));
    zassert_ok(tr_set_port_mode(servers[0], MB_MODE_CALLBACK));
    zassert_equal(tr_reg_table_func(MB_MODE_CALLBACK, NULL), -EBUSY);
    handler_result = -1;
    uint16_t word;
    zassert_equal(modbus_read_input_regs(client, 7, 100, &word, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(client, 7, 100, 5), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(atomic_get(&writes), 0);
    handler_result = 0;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 4321);
    zassert_ok(tr_set_port_mode(servers[0], MB_MODE_SLAVE));
    zassert_ok(modbus_write_holding_reg(client, 7, 100, 5));
    zassert_equal(atomic_get(&writes), 1);
    zassert_ok(tr_reg_table_func(MB_MODE_CALLBACK, NULL));
}

ZTEST(reference_ports, test_command_numbers_do_not_alias_native_stats)
{
    uint32_t guard[4] = {0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5};
    for (uint32_t cmd = 0; cmd < MB_PORT_CTRL_ENUM_MAX; ++cmd) {
        if (cmd == MB_PORT_GET_RECV_MB_TIMES || cmd == MB_PORT_CLR_RECV_MB_TIMES) { continue; }
        zassert_equal(tr_mb_port_work_control(servers[0], cmd, guard), -ENOTSUP);
    }
    for (size_t i = 0; i < ARRAY_SIZE(guard); ++i) { zassert_equal(guard[i], 0xa5a5a5a5); }
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_ok(tr_mb_port_work_control(servers[0], MB_PORT_GET_RECV_MB_TIMES, guard));
    zassert_equal(guard[0], 1);
    zassert_equal(guard[1], 0xa5a5a5a5);
    zassert_ok(tr_mb_port_work_control(servers[0], MB_PORT_CLR_RECV_MB_TIMES, NULL));
    zassert_ok(tr_mb_port_work_control(servers[0], MB_PORT_GET_RECV_MB_TIMES, guard));
    zassert_equal(guard[0], 0);
    struct tr_port_stats stats;
    zassert_ok(tr_port_control(servers[0], TR_PORT_GET_STATS, &stats));
    zassert_equal(stats.responses, 1);
}

static int initialize_port(uint16_t port)
{
    zassert_equal(port, 0);
    zassert_equal(atomic_get(&init_calls), 0);
    ++port_initializations;
    return 0;
}

static int transform_value(CO_SET_VALUE_TRANS_S *data)
{
    zassert_equal(data->sig_size, 2);
    sys_put_le16(sys_get_le16(data->trans_val) + 7, data->trans_val);
    return 0;
}

static void write_complete(PARA_DATA_S *data)
{
    zassert_equal(data->set_num, 1);
    ++completions;
}

ZTEST(reference_ports, test_fast_write_preflight_conversion_and_no_dm)
{
    zassert_ok(co_init_func_reg(0, initialize_port));
    zassert_ok(co_dev_init(1));
    zassert_equal(port_initializations, 1);
    struct dp_point_state before_state, after_state;
    zassert_ok(dp_read((struct dp_key){1, 3}, &before_state));
    DATA_SET_S items[] = {
        {.dev_id = 1, .sig_id = 3, .sig_size = 2, .p_value.u16 = 88},
        {.dev_id = 99, .sig_id = 1, .sig_size = 2, .p_value.u16 = 88},
    };
    PARA_DATA_S data = {.set_num = 2, .p_set_data = items};
    zassert_ok(co_set_value_trans_cb_reg(transform_value));
    zassert_equal(co_dp_fast_set(&data, write_complete), -EINVAL);
    zassert_equal(k_msgq_num_used_get(&transfers), 0);
    zassert_equal(completions, 0);
    data.set_num = 1;
    items[0].sig_id = 1; /* FC4 input registers are never inferred writable. */
    zassert_equal(co_dp_fast_set(&data, write_complete), -ENOTSUP);
    zassert_equal(k_msgq_num_used_get(&transfers), 0);
    items[0].sig_id = 3;
    zassert_ok(co_dp_fast_set(&data, write_complete));
    zassert_equal(south_value, 95);
    zassert_equal(completions, 1);
    zassert_equal(items[0].p_value.u16, 88);
    zassert_ok(dp_read((struct dp_key){1, 3}, &after_state));
    zassert_equal(before_state.value.words[0], after_state.value.words[0]);
}

static void add_crc(uint8_t *frame, size_t size)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size - 2; ++i) {
        crc ^= frame[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
        }
    }
    sys_put_le16(crc, frame + size - 2);
}

ZTEST(reference_ports, test_rtu_injection_validates_crc_unit_and_mapping)
{
    uint8_t frame[] = {1, 4, 2, 0x12, 0x34, 0, 0};
    add_crc(frame, sizeof(frame));
    zassert_ok(co_anlysis_mb_pkg(1, 100, frame, sizeof(frame)));
    k_sleep(K_MSEC(30));
    struct dp_point_state state;
    for (uint16_t sig = 1; sig <= 2; ++sig) {
        zassert_ok(dp_read((struct dp_key){1, sig}, &state));
        zassert_equal(state.value.words[0], 0x1234);
    }
    frame[3] ^= 1;
    zassert_equal(co_anlysis_mb_pkg(1, 100, frame, sizeof(frame)), -EBADMSG);
    frame[0] = 2; add_crc(frame, sizeof(frame));
    zassert_equal(co_anlysis_mb_pkg(1, 100, frame, sizeof(frame)), -EBADMSG);
    frame[0] = 1; add_crc(frame, sizeof(frame));
    zassert_equal(co_anlysis_mb_pkg(1, 200, frame, sizeof(frame)), -ENOENT);
    uint8_t exception[] = {1, 0x84, 2, 0, 0};
    add_crc(exception, sizeof(exception));
    zassert_equal(co_anlysis_mb_pkg(1, 100, exception, sizeof(exception)), -EIO);
    zassert_ok(dp_read((struct dp_key){1, 1}, &state));
    zassert_equal(state.value.words[0], 0x1234);
}

ZTEST(reference_ports, test_connection_status_tracks_adu_timeout)
{
    struct tr_connection_status state;
    zassert_ok(tr_port_conn_status_config(servers[0], true));
    zassert_ok(tr_port_connection_get(servers[0], &state));
    zassert_true(state.enabled); zassert_false(state.connected);
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_ok(tr_port_connection_get(servers[0], &state));
    zassert_true(state.connected);
    k_sleep(K_MSEC(CONFIG_TR_CONN_TIMEOUT_MS + 20));
    zassert_ok(tr_port_connection_get(servers[0], &state));
    zassert_false(state.connected);
    zassert_ok(tr_port_conn_status_config(servers[0], false));
    zassert_ok(tr_port_connection_get(servers[0], &state));
    zassert_false(state.enabled); zassert_false(state.connected);
}

ZTEST_SUITE(reference_ports, NULL, setup, before, NULL, NULL);
