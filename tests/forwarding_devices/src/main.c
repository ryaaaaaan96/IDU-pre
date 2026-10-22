#include <dataprocess/points.h>
#include <transmit/transmit.h>
#include <transmit/forward.h>
#include <dataprocess/table_registry.h>
#include <platform_callback.h>
#include <dataprocess/params.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

static int client, servers[3], target;
static const DM_SIG_CFG_S process[] = {
    {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .data.def_value.u16 = 111},
};
static const DM_SIG_CFG_S remote_process[] = {
    {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16},
    {.sig_id = 2, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_COLLECT | FLAG_PERSISTENCE | FLAG_PERSISTENCE_SYNC},
    {.sig_id = 3, .sig_size = 2, .data_type = DATA_TYPE_U16, .sig_flag = FLAG_COLLECT},
};
static const MODBUS_RTU_TABLE_S collect[] = {
    {.sig_id = 1, .reg_type = 4, .reg_addr = 10, .reg_num = 1,
     .period_ms = 500, .flag = 0},
    {.sig_id = 2, .reg_type = 3, .reg_addr = 20, .reg_num = 1, .flag = 2, .period_ms = 0},
    {.sig_id = 3, .reg_type = 3, .reg_addr = 21, .reg_num = 1, .flag = 2, .period_ms = 0},
};
static const MB_TABLE_CELL_T maps[] = {
    {.reg_type = 4, .reg_addr = 100, .reg_num = 1, .dev_id = 10, .sig_id = 1},
    {.reg_type = 3, .reg_addr = 200, .reg_num = 1, .dev_id = 10, .sig_id = 2},
    {.reg_type = 3, .reg_addr = 201, .reg_num = 1, .dev_id = 10, .sig_id = 3},
    {.reg_type = 4, .reg_addr = 100, .reg_num = 1, .dev_id = 11, .sig_id = 1},
    {.reg_type = 3, .reg_addr = 200, .reg_num = 1, .dev_id = 11, .sig_id = 2},
    {.reg_type = 3, .reg_addr = 201, .reg_num = 1, .dev_id = 11, .sig_id = 3},
};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, processes) = {
    .id = 1, .type = PRO_DATA_PROCESS, .rows = process, .count = 1};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, polls) = {
    .id = 2, .type = PRO_MB_MASTER, .rows = collect, .count = ARRAY_SIZE(collect)};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, first_map) = {
    .id = 3, .type = PRO_MB_SLAVE, .rows = &maps[0], .count = 3};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, second_map) = {
    .id = 4, .type = PRO_MB_SLAVE, .rows = &maps[3], .count = 3};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, remote_points) = {
    .id = 5, .type = PRO_DATA_PROCESS, .rows = remote_process, .count = ARRAY_SIZE(remote_process)};
static const struct data_device_cfg data_devices[] = {
    {.dev_id = 1, .process_table = 1, .collect_table = 0},
    {.dev_id = 10, .process_table = 5, .collect_table = 2, .collect_port = 0, .collect_addr = 1},
    {.dev_id = 11, .process_table = 5, .collect_table = 2, .collect_port = 0, .collect_addr = 2},
};
static DEV_TABLE_S devices[3];
static const DEV_TABLE_S *get_devices(void) { return devices; }
static uint16_t get_count(void) { return ARRAY_SIZE(devices); }
K_SEM_DEFINE(segment_done, 0, 1);
static unsigned segment_calls;
static int segment(int iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    ARG_UNUSED(iface); ++segment_calls; k_sem_give(&segment_done);
    if (rx->length == 1024) { return TR_MODBUS_SILENT; }
    if (rx->length != 4 || rx->fc != 4) { return -ENOENT; }
    tx->length = 3; tx->data[0] = 2; sys_put_be16(77, tx->data + 1);
    return 0;
}
static const MB_TABLE_SEG_T protocol_segments[] = {
    {.reg_addr = 20003, .end_addr = 20003, .func = segment},
};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, protocol_segment_table) = {
    .id = 6, .type = PRO_ADDR_SLAVE, .rows = protocol_segments, .count = 1};
static const struct tr_forward_cfg forwarding[] = {
    {.forward_id = 1, .port_id = 1, .transmit_table = 3, .protocol_segments = 6, .route_broadcast = true},
    {.forward_id = 2, .port_id = 2, .transmit_table = 4, .protocol_segments = 0, .route_broadcast = false},
    {.forward_id = 3, .port_id = 3, .transmit_table = 3, .protocol_segments = 0, .route_broadcast = false},
};

static uint16_t south_values[2][2];
static unsigned south_writes[2], south_reads[2];
static int south_read(void *user, uint16_t dev, uint16_t address, uint16_t *words, size_t count)
{
    ARG_UNUSED(user); zassert_true(dev == 10 || dev == 11);
    zassert_equal(count, 1); zassert_true(address == 20 || address == 21);
    ++south_reads[dev - 10]; *words = south_values[dev - 10][address - 20];
    return 0;
}
static int south_write(void *user, uint16_t dev, uint16_t address, const uint16_t *words, size_t count)
{
    ARG_UNUSED(user); zassert_true(dev == 10 || dev == 11);
    zassert_equal(count, 1); zassert_true(address == 20 || address == 21);
    ++south_writes[dev - 10]; south_values[dev - 10][address - 20] = *words;
    return 0;
}
static int request(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(servers[target], adu);
}
static int response(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(client, adu);
}
static void *setup(void)
{
    zassert_ok(dp_data_device_bind(data_devices, ARRAY_SIZE(data_devices), devices));
    pltcb_reg_cfg_get_dev_tbl(get_devices); pltcb_reg_cfg_get_dev_num(get_count);
    zassert_ok(dp_init());
    const struct dp_params_transport transport = {.read = south_read, .write = south_write};
    zassert_ok(dp_params_set_transport(&transport));
    zassert_ok(dp_params_init(dp_config(), DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition))));
    uint16_t values[] = {111, 222};
    for (size_t i = 0; i < 2; ++i) {
        zassert_ok(dp_set_sig_value(10 + i, 1, sizeof(values[i]), (uint8_t *)&values[i]));
    }
    client = modbus_iface_get_by_name("RAW_0");
    servers[0] = modbus_iface_get_by_name("RAW_1");
    servers[1] = modbus_iface_get_by_name("RAW_2");
    servers[2] = modbus_iface_get_by_name("RAW_3");
    struct tr_forward_cfg invalid = forwarding[0];
    invalid.transmit_table = 999;
    zassert_equal(tr_forward_attach(servers[0], &invalid), -ENOENT);
    invalid.transmit_table = 1;
    zassert_equal(tr_forward_attach(servers[0], &invalid), -EINVAL);
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        int err = tr_forward_attach(servers[i], &forwarding[i]);
        zassert_ok(err, "entry %u: %d", (unsigned)i, err);
        struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
            .server = {.unit_id = 7, .user_cb = tr_callbacks()},
            .rawcb = {.raw_tx_cb = response}};
        zassert_ok(tr_modbus_init_server(servers[i], param, tr_register_write, true));
    }
    struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
        .rx_timeout = 20000, .rawcb = {.raw_tx_cb = request}};
    zassert_ok(modbus_init_client(client, param));
    return NULL;
}
ZTEST(forwarding_devices, test_invalid_declaration_preserves_active_entry)
{
    struct tr_forward_cfg invalid = forwarding[1];
    invalid.forward_id = 99; invalid.port_id = 99;
    zassert_equal(tr_forward_attach(servers[0], &invalid), -EEXIST);
    uint16_t word; target = 0;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1)); zassert_equal(word, 111);
    invalid = forwarding[1]; invalid.port_id = 99;
    zassert_equal(tr_forward_attach(15, &invalid), -EEXIST); /* Duplicate forward identity. */
}
ZTEST(forwarding_devices, test_same_address_reads_selected_entry)
{
    uint16_t word;
    target = 0;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 111);
    target = 1;
    zassert_ok(modbus_read_input_regs(client, 7, 100, &word, 1));
    zassert_equal(word, 222);
    zassert_equal(modbus_read_input_regs(client, 7, 101, &word, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(tr_read_register(4, 100, &word), -ENOTUNIQ);
    zassert_equal(tr_read(99, 4, 100, &word, 1), -ENOENT);
}
ZTEST(forwarding_devices, test_writes_use_selected_data_identity_and_persist)
{
    target = 0;
    zassert_ok(modbus_write_holding_reg(client, 7, 200, 123));
    zassert_equal(south_values[0][0], 123); zassert_equal(south_values[1][0], 0);
    target = 1;
    zassert_ok(modbus_write_holding_reg(client, 7, 200, 456));
    zassert_equal(south_values[0][0], 123); zassert_equal(south_values[1][0], 456);
    uint16_t word;
    target = 2; /* A second entrance shares the first const table. */
    zassert_ok(modbus_read_holding_regs(client, 7, 200, &word, 1));
    zassert_equal(word, 123);
    unsigned writes = south_writes[0], reads = south_reads[0];
    zassert_ok(modbus_write_holding_reg(client, 7, 201, 99));
    zassert_equal(south_writes[0], writes + 1); zassert_equal(south_reads[0], reads);
    zassert_ok(dp_params_reload());
    zassert_equal(south_writes[0], writes + 1); /* No command replay. */
    target = 0;
    zassert_ok(modbus_read_holding_regs(client, 7, 200, &word, 1)); zassert_equal(word, 123);
    target = 1;
    zassert_ok(modbus_read_holding_regs(client, 7, 200, &word, 1)); zassert_equal(word, 456);
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_false(status.flags & DP_PARAM_STORAGE_RESET);
}
ZTEST(forwarding_devices, test_protocol_segments_belong_to_selected_entry)
{
    uint16_t word;
    target = 0;
    zassert_ok(modbus_read_input_regs(client, 7, 20003, &word, 1)); zassert_equal(word, 77);
    unsigned calls = segment_calls;
    target = 1;
    zassert_equal(modbus_read_input_regs(client, 7, 20003, &word, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(segment_calls, calls);
    target = 0;
    zassert_equal(modbus_write_holding_reg(client, 7, 20003, 99), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(segment_calls, calls + 1); /* A segment error never falls through. */
    struct modbus_adu rx = {.unit_id = 0, .fc = 16, .length = 1024};
    sys_put_be16(20003, rx.data);
    k_sem_reset(&segment_done);
    zassert_ok(modbus_raw_submit_rx(servers[0], &rx));
    zassert_ok(k_sem_take(&segment_done, K_SECONDS(1)));
    zassert_equal(segment_calls, calls + 2);
    zassert_ok(modbus_raw_submit_rx(servers[1], &rx));
    zassert_equal(k_sem_take(&segment_done, K_MSEC(10)), -EAGAIN);
    zassert_equal(segment_calls, calls + 2);
}
ZTEST_SUITE(forwarding_devices, NULL, setup, NULL, NULL, NULL);
