#include "simulation.h"
#include "protocol/inc/protocol.h"
#include <collect/port.h>
#include <collect/simulator.h>
#include <transmit/server.h>
#include <transmit/registers.h>
#include "process/idu_process.h"
#include "protocol/inc/signal_ids.h"
#include <collect/message.h>
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <transmit/modbus_adapter.h>
#include <transmit/transmit.h>
#include "modbus_internal.h" /* TX-state injection; no UART electrical simulation. */
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>

static int server, client, fail_init;
static struct modbus_context *emulated_tx_context;
static const struct flash_area *storage;
static struct modbus_iface_param server_param;
static int fail_after = -1;
static K_SEM_DEFINE(config_done, 0, 1);
static int config_error;
static bool capture_only;
static struct modbus_adu captured_adu;
static K_SEM_DEFINE(captured, 0, 1);
static atomic_t inject_status, status_reads;

void __real_dp_params_get_status(struct dp_param_status *status);
void __wrap_dp_params_get_status(struct dp_param_status *status)
{
    if (!atomic_get(&inject_status)) {
        __real_dp_params_get_status(status);
        return;
    }
    /* Every call observes a new transaction, including a 16-bit carry. */
    uint32_t read = atomic_inc(&status_reads);
    *status = (struct dp_param_status){
        .flags = read ? DP_PARAM_PENDING : DP_PARAM_STORAGE_READY,
        .error = read ? -EIO : 0,
        .sequence = 0x1234ffff + read,
    };
}

int __real_modbus_init_server(int iface, struct modbus_iface_param param);
int __wrap_modbus_init_server(int iface, struct modbus_iface_param param)
{
    if (iface == server && fail_init) { --fail_init; return -EIO; }
    if (iface == server && emulated_tx_context) {
        /* The native fixture uses RAW again once backend recovery is reached. */
        emulated_tx_context->mode = MODBUS_MODE_RAW;
        emulated_tx_context = NULL;
    }
    return __real_modbus_init_server(iface, param);
}

static int inject_flash_write(const struct device *dev, off_t offset, uint8_t byte)
{
    ARG_UNUSED(dev); ARG_UNUSED(offset);
    if (!fail_after) { return -EIO; }
    if (fail_after > 0) { --fail_after; }
    return byte;
}
static const struct flash_simulator_cb flash_cb = {.write_byte = inject_flash_write};

static int request(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    return modbus_raw_submit_rx(server, adu);
}
static int response(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    if (capture_only) { captured_adu = *adu; k_sem_give(&captured); return 0; }
    return modbus_raw_submit_rx(client, adu);
}
static void completed(int iface, int err, void *user)
{
    ARG_UNUSED(user); zassert_equal(iface, server);
    config_error = err; k_sem_give(&config_done);
}

static uint32_t checksum(const struct flash_area *area)
{
    uint32_t crc = 0;
    uint8_t block[64];
    for (size_t i = 0; i < area->fa_size; i += sizeof(block)) {
        zassert_ok(flash_area_read(area, i, block, sizeof(block)));
        crc = crc32_ieee_update(crc, block, sizeof(block));
    }
    return crc;
}
static uint16_t saved(void)
{
    struct dp_point_state state;
    zassert_ok(dp_params_read((struct dp_key){1, IDU_SIG_SAVED_ADDRESS}, &state));
    return state.value.words[0];
}
static void fresh_nvs(void)
{
    zassert_ok(flash_area_erase(storage, 0, storage->fa_size));
}
static void mount_fixture(struct nvs_fs *fs)
{
    struct flash_pages_info page;
    zassert_ok(flash_get_page_info_by_offs(flash_area_get_device(storage), storage->fa_off, &page));
    *fs = (struct nvs_fs){.flash_device = flash_area_get_device(storage),
        .offset = storage->fa_off, .sector_size = page.size, .sector_count = storage->fa_size / page.size};
    zassert_ok(nvs_mount(fs));
}

/* Frozen pre-migration default-profile image: schema fa1be938, four parameters,
 * period 750, label ABC, remote setpoint 700, offset -12, sequence 42. */
static const uint8_t incompatible_snapshot[] = {
    0x49,0x44,0x55,0x50,0x01,0x00,0x04,0x00,0x38,0xe9,0x1b,0xfa,0x2a,0x00,0x00,0x00,
    0x01,0x00,0x18,0x79,0x00,0x01,0xee,0x02,0x01,0x00,0x19,0x79,0x04,0x08,0x42,0x41,
    0x00,0x43,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,
    0x0c,0x7b,0x00,0x01,0xbc,0x02,0x0a,0x00,0x0d,0x7b,0x03,0x02,0xf4,0xff,0xff,0xff,
};
BUILD_ASSERT(sizeof(incompatible_snapshot) == 64);

static void write_incompatible_fixture(bool intent)
{
    fresh_nvs();
    struct nvs_fs fs;
    mount_fixture(&fs);
    zassert_equal(nvs_write(&fs, 1, incompatible_snapshot, sizeof(incompatible_snapshot)), sizeof(incompatible_snapshot));
    if (intent) {
        uint8_t pending[88] = {0};
        sys_put_le32(0x4a554449, pending); sys_put_le16(1, pending + 4);
        pending[6] = 1; pending[7] = 1; sys_put_le16(40000, pending + 8);
        sys_put_le32(43, pending + 12); sys_put_le16(64, pending + 16); sys_put_le16(10, pending + 18);
        sys_put_le16(700, pending + 20); sys_put_le16(888, pending + 22);
        memcpy(pending + 24, incompatible_snapshot, sizeof(incompatible_snapshot));
        sys_put_le32(43, pending + 24 + 12); sys_put_le16(888, pending + 24 + 52);
        zassert_equal(nvs_write(&fs, 2, pending, sizeof(pending)), sizeof(pending));
    }
}

static int attach_profile(void)
{
    return idu_forwarding.attach(server, idu_forwarding.protocol);
}

static void *setup(void)
{
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)), &storage));
    flash_simulator_set_callbacks(flash_area_get_device(storage), &flash_cb);
    zassert_ok(idu_register_point_tables());
    zassert_ok(dp_init());
    const struct dp_equip_cfg *cfg = dp_config();
    zassert_ok(idu_ports_init());
    int south = co_port_open(&idu_collect_port); zassert_true(south >= 0);
    zassert_ok(co_init(cfg, south, true));
    zassert_ok(dp_params_init(cfg, storage->fa_id));
    zassert_ok(dp_process_init_device(0));
    server = modbus_iface_get_by_name("RAW_2"); client = modbus_iface_get_by_name("RAW_3");
    server_param = (struct modbus_iface_param){.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = tr_callbacks()}, .rawcb = {.raw_tx_cb = response}};
    struct modbus_iface_param client_param = {.mode = MODBUS_MODE_RAW, .rx_timeout = 50000,
                                              .rawcb = {.raw_tx_cb = request}};
    zassert_ok(attach_profile());
    if (idu_forwarding.configure) { zassert_ok(idu_forwarding.configure(&server_param)); }
    zassert_ok(tr_modbus_init_server(server, server_param, tr_register_write, true));
    zassert_ok(modbus_init_client(client, client_param));
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture); fail_after = -1; fail_init = 0;
    if (getenv("IDU_FORWARDING_PHASE")) { return; }
    fresh_nvs(); zassert_ok(dp_params_reload());
    zassert_ok(attach_profile());
    zassert_ok(co_simulator_set_register(&idu_collect_simulator, 3, 40000, 100));
    struct dp_point_batch batch = {.source = DP_SOURCE_LOCAL, .count = 2,
        .points = {{.key = {1, IDU_SIG_ADDRESS}, .width = 1, .value.words = {16}},
                   {.key = {1, IDU_SIG_DI}, .width = 1, .value.words = {0xa55a}}}};
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    zassert_ok(dp_wait_processed(batch.ticket, K_MSEC(200)));
}

ZTEST(forwarding, test_address_filter_and_parameter_validation)
{
    uint16_t word;
    zassert_equal(saved(), 1);
    zassert_equal(modbus_read_holding_regs(client, 247, 30459, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_write_holding_reg(client, 247, 30459, 37), -ETIMEDOUT);
    zassert_equal(saved(), 1);
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 246));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 246);
    zassert_ok(modbus_read_input_regs(client, 16, 8152, &word, 1)); zassert_equal(word, 0xa55a);
    zassert_equal(modbus_write_holding_reg(client, 16, 30459, 0), MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(modbus_write_holding_reg(client, 16, 30459, 247), MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(saved(), 246);
}

ZTEST(forwarding, test_application_collection_uses_virtual_ports)
{
    struct modbus_adu request = {.fc = 4, .length = 4, .data = {0x1f, 0xd8, 0, 1}};
    zassert_ok(co_transfer(10, &request));
    zassert_equal(request.length, 3);
    zassert_equal(request.data[0], 2);
    zassert_equal(sys_get_be16(request.data + 1), 5);
}

ZTEST(forwarding, test_unmapped_registers_and_contiguous_reads)
{
    uint16_t words[15];
    zassert_equal(modbus_read_holding_regs(client, 16, 8149, words, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(client, 16, 8159, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(client, 16, 39281, 0), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_read_input_regs(client, 16, 2513, words, 2), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(modbus_read_input_regs(client, 16, 8151, words, 2));
    zassert_equal(words[0], IDU_PROTOCOL_VERSION >> 16); zassert_equal(words[1], 0xa55a);
    zassert_ok(modbus_read_input_regs(client, 16, 2581, words, 1)); zassert_equal(words[0], 0x4944);
    zassert_ok(modbus_read_input_regs(client, 16, 2581, words, 15));
}

ZTEST(forwarding, test_parameter_broadcasts_have_no_reply_or_side_effect)
{
    uint16_t word, value = 42;
    zassert_equal(modbus_read_input_regs(client, 0, 8149, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_write_holding_regs(client, 0, 30459, &value, 1), -ETIMEDOUT);
    zassert_equal(modbus_write_holding_reg(client, 0, 30459, 43), -ETIMEDOUT);
    zassert_equal(saved(), 1);
    value = 800;
    zassert_equal(modbus_write_holding_regs(client, 0, 31000, &value, 1), -ETIMEDOUT);
    zassert_equal(idu_sample_period_ms(), 500);
    zassert_ok(modbus_write_holding_reg(client, 16, 31000, 800));
    zassert_equal(idu_sample_period_ms(), 800);
}

ZTEST(forwarding, test_malformed_and_unsupported_requests_return_exceptions)
{
    const struct modbus_adu malformed[] = {
        {.unit_id=16,.fc=16,.length=7,.data={0x76,0xfb,0,2,2,0,55}},
        {.unit_id=16,.fc=3,.length=4,.data={0x76,0xfb,0,0}},
        {.unit_id=16,.fc=3,.length=5,.data={0x76,0xfb,0,1,0}},
        {.unit_id=16,.fc=6,.length=5,.data={0x76,0xfb,0,55,0}},
        {.unit_id=16,.fc=5,.length=4,.data={0x76,0xfb,0xff,0}},
    };
    capture_only = true;
    k_sem_reset(&captured);
    for (size_t i = 0; i < ARRAY_SIZE(malformed); ++i) {
        struct modbus_adu adu = malformed[i];
        zassert_ok(modbus_raw_submit_rx(server, &adu));
        zassert_ok(k_sem_take(&captured, K_MSEC(100)));
        zassert_equal(captured_adu.fc, adu.fc | 0x80);
        zassert_equal(captured_adu.length, 1);
        zassert_equal(saved(), 1);
    }
    capture_only = false;
}

ZTEST(forwarding, test_parameter_save_and_reload)
{
    zassert_equal(saved(), 1);
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 88));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 88);
    fresh_nvs();
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 1);
}

ZTEST(forwarding, test_failed_address_save_has_no_success_ack)
{
    fail_after = 0;
    zassert_equal(modbus_write_holding_reg(client, 16, 30459, 83), MODBUS_EXC_SERVER_DEVICE_FAILURE);
    fail_after = -1;
    zassert_equal(saved(), 1);
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_false(status.flags & DP_PARAM_STORAGE_READY);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 1);
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 83));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 83);
}

ZTEST(forwarding, test_disabled_iap_has_no_registers)
{
    uint16_t words[2];
    zassert_equal(modbus_read_holding_regs(client, 16, 20003, words, 1), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_read_holding_regs(client, 16, 31996, words, 2), MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(client, 16, 20049, 3), MODBUS_EXC_ILLEGAL_DATA_ADDR);
}

ZTEST(forwarding, test_previous_schema_is_rejected_without_migration)
{
    write_incompatible_fixture(false);
    uint32_t previous = checksum(storage);
    zassert_equal(dp_params_reload(), -EPROTO);
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_false(status.flags & DP_PARAM_STORAGE_READY);
    zassert_false(status.flags & DP_PARAM_SCHEMA_MIGRATED);
    zassert_equal(dp_params_flush(), -EROFS);
    zassert_equal(checksum(storage), previous);
}

ZTEST(forwarding, test_previous_schema_intent_never_replays)
{
    write_incompatible_fixture(true);
    uint32_t previous = checksum(storage);
    unsigned writes = co_simulator_write_count(&idu_collect_simulator);
    zassert_equal(dp_params_reload(), -EPROTO);
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), writes);
    zassert_equal(checksum(storage), previous);
}

ZTEST(forwarding, test_default_snapshot_torn_write_is_retryable)
{
    for (int cut = 0; cut < 110; ++cut) {
        fresh_nvs(); fail_after = cut;
        int err = dp_params_reload(); fail_after = -1;
        zassert_true(err == 0 || err == -EIO, "cut=%d err=%d", cut, err);
        zassert_ok(dp_params_reload()); zassert_equal(saved(), 1);
    }
}

ZTEST(forwarding, test_recovery_reinitializes_and_rolls_back)
{
    struct tr_modbus_health before, after;
    zassert_ok(tr_modbus_get_health(server, &before));
    for (int attempt = 0; attempt < 3; ++attempt) {
        fail_init = attempt;
        zassert_ok(tr_modbus_recover(server, completed, NULL));
        zassert_ok(k_sem_take(&config_done, K_SECONDS(3)));
        zassert_equal(config_error, attempt == 0 ? 0 : attempt == 1 ? -EIO : -ENOTRECOVERABLE);
        zassert_ok(tr_modbus_get_health(server, &after));
        zassert_equal(after.recoveries, before.recoveries + attempt + 1);
        if (attempt < 2) {
            uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 30459, &word, 1));
            zassert_equal(word, 1);
        } else { zassert_equal(tr_modbus_service(server, 20000), -ENOTRECOVERABLE); }
    }
    zassert_ok(tr_modbus_init_server(server, server_param, tr_register_write, true));
}

ZTEST(forwarding, test_idle_service_recovers_without_resetting_parameters)
{
    struct tr_modbus_health before, after;
    zassert_ok(tr_modbus_get_health(server, &before));
    k_sleep(K_MSEC(30)); zassert_ok(tr_modbus_service(server, 20));
    for (int i = 0; i < 100; ++i) {
        zassert_ok(tr_modbus_get_health(server, &after));
        if (!after.changing && after.recoveries > before.recoveries) { break; }
        k_sleep(K_MSEC(5));
    }
    zassert_equal(after.recoveries, before.recoveries + 1); zassert_ok(after.last_error);
    zassert_equal(saved(), 1);
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 30459, &word, 1));
}

ZTEST(forwarding, test_stuck_tx_state_can_be_forced_back_to_receive)
{
    struct modbus_context *ctx = modbus_get_context(server);
    zassert_not_null(ctx);
    /* Emulate a lost TX-complete event. No native UART driver is involved. */
    ctx->mode = MODBUS_MODE_RTU;
    atomic_clear_bit(&ctx->state, MODBUS_STATE_RX_ENABLED);
    zassert_ok(tr_modbus_reconfigure(server, server_param, completed, NULL));
    zassert_ok(k_sem_take(&config_done, K_SECONDS(3))); zassert_equal(config_error, -ETIMEDOUT);
    emulated_tx_context = ctx;
    zassert_ok(tr_modbus_recover(server, completed, NULL));
    zassert_ok(k_sem_take(&config_done, K_SECONDS(3))); zassert_ok(config_error);
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 30459, &word, 1));
    zassert_equal(word, 1);
}

static K_SEM_DEFINE(handler_entered, 0, 1);
static K_SEM_DEFINE(handler_release, 0, 1);
static K_SEM_DEFINE(handler_finished, 0, 1);
static int stalled_handler(int iface, const struct modbus_adu *rx, struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(rx); ARG_UNUSED(tx); ARG_UNUSED(user);
    k_sem_give(&handler_entered);
    k_sem_take(&handler_release, K_FOREVER);
    k_sem_give(&handler_finished);
    return TR_MODBUS_SILENT;
}

ZTEST(forwarding, test_stuck_handler_has_bounded_recovery_and_watchdog_error)
{
    zassert_ok(tr_modbus_set_router(server, stalled_handler, NULL));
    struct modbus_adu rx = {.unit_id=16, .fc=3, .length=4, .data={0x76,0xfb,0,1}};
    zassert_ok(modbus_raw_submit_rx(server, &rx));
    zassert_ok(k_sem_take(&handler_entered, K_SECONDS(1)));
    zassert_ok(tr_modbus_service(server, 20)); k_sleep(K_MSEC(30));
    zassert_equal(tr_modbus_service(server, 20), -ETIMEDOUT);
    zassert_ok(tr_modbus_recover(server, completed, NULL));
    zassert_ok(k_sem_take(&config_done, K_SECONDS(3))); zassert_equal(config_error, -ETIMEDOUT);
    k_sem_give(&handler_release);
    zassert_ok(k_sem_take(&handler_finished, K_SECONDS(1))); k_sleep(K_MSEC(20));
    zassert_ok(attach_profile());
    zassert_ok(tr_modbus_recover(server, completed, NULL));
    zassert_ok(k_sem_take(&config_done, K_SECONDS(3))); zassert_ok(config_error);
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 30459, &word, 1));
}

ZTEST(forwarding, test_reboot_probe)
{
    const char *phase = getenv("IDU_FORWARDING_PHASE");
    if (!phase) { ztest_test_skip(); return; }
    if (!strcmp(phase, "write")) {
        zassert_equal(saved(), 1);
        zassert_ok(modbus_write_holding_reg(client, 16, 30459, 91));
    } else if (!strcmp(phase, "verify")) {
        zassert_equal(saved(), 91);
        uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 30459, &word, 1));
        zassert_equal(word, 91);
    } else { zassert_unreachable("unknown phase"); }
}

ZTEST(forwarding, test_parameter_status_through_product_router)
{
    struct dp_param_status status;
    uint16_t value;
    dp_params_get_status(&status);
    zassert_ok(modbus_read_holding_regs(client, 16, 32000, &value, 1));
    zassert_equal(value, status.flags);
}

ZTEST(forwarding, test_invalid_configuration_preserves_registered_router)
{
    const struct tr_routes routes = {.count = 1};
    const struct tr_register_service invalid = {.routes = &routes};
    zassert_equal(tr_register_attach(server, &invalid), -EINVAL);
    uint16_t value;
    zassert_ok(modbus_read_holding_regs(client, 16, 30459, &value, 1));
}

ZTEST(forwarding, test_status_response_uses_one_transaction_snapshot)
{
    uint16_t words[4];
    atomic_clear(&status_reads);
    atomic_set(&inject_status, 1);
    int err = modbus_read_holding_regs(client, 16, 32000, words, ARRAY_SIZE(words));
    atomic_clear(&inject_status);
    zassert_ok(err);
    zassert_equal(words[0], DP_PARAM_STORAGE_READY);
    zassert_equal(words[1], 0);
    zassert_equal(words[2], 0xffff);
    zassert_equal(words[3], 0x1234);
    zassert_equal(atomic_get(&status_reads), 1);
}

ZTEST(forwarding, test_partial_status_read_uses_one_fresh_snapshot)
{
    uint16_t words[3];
    atomic_clear(&status_reads);
    atomic_set(&inject_status, 1);
    int err = modbus_read_holding_regs(client, 16, 32001, words, ARRAY_SIZE(words));
    atomic_clear(&inject_status);
    zassert_ok(err);
    zassert_equal(words[0], 0);
    zassert_equal(words[1], 0xffff);
    zassert_equal(words[2], 0x1234);
    zassert_equal(atomic_get(&status_reads), 1);
}

static int segment_reply(int iface, const struct modbus_adu *rx,
                         struct modbus_adu *tx, void *user)
{
    zassert_equal(iface, server);
    zassert_equal(sys_get_be16(rx->data), 33000);
    zassert_equal(user, &server);
    tx->length = 3;
    tx->data[0] = 2;
    sys_put_be16(0x1234, tx->data + 1);
    return 0;
}

ZTEST(forwarding, test_register_service_retains_segment_callbacks)
{
    static const struct tr_segment segments[] = {
        {.begin = 33000, .end = 33000, .function = 3,
         .handler = segment_reply, .user = &server},
    };
    static const struct tr_routes routes = {segments, ARRAY_SIZE(segments)};
    static const struct tr_register_service service = {.routes = &routes};
    zassert_ok(tr_register_attach(server, &service));
    uint16_t words[2];
    zassert_ok(modbus_read_holding_regs(client, 16, 33000, words, 1));
    zassert_equal(words[0], 0x1234);
    zassert_equal(modbus_read_holding_regs(client, 16, 33000, words, 2),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    /* Nonsegment requests continue through the process/transmit point tables. */
    zassert_ok(modbus_read_holding_regs(client, 16, 32000, words, 1));
    zassert_equal(words[0], DP_PARAM_STORAGE_READY);
    zassert_ok(modbus_write_holding_reg(client, 16, 31000, 750));
    zassert_ok(modbus_write_holding_reg(client, 16, 31090, 0xa55a));
    zassert_ok(modbus_read_holding_regs(client, 16, 31000, words, 1));
    zassert_equal(words[0], 500);
}

static atomic_t extension_calls;
static int protocol_extension(int iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    zassert_equal(iface, server);
    if (rx->length < 2 || sys_get_be16(rx->data) != 20049) { return -ENOENT; }
    atomic_inc(&extension_calls);
    zassert_equal(rx->length, 1029);
    zassert_equal(sys_get_be16(rx->data + 2), 512);
    zassert_equal(rx->data[1028], 0xa5);
    memcpy(tx->data, rx->data, 4);
    tx->length = 4;
    return rx->unit_id ? 0 : TR_MODBUS_SILENT;
}

ZTEST(forwarding, test_extension_receives_complete_adu_before_standard_validation)
{
    static const struct tr_register_service service = {.extension = protocol_extension};
    static struct modbus_adu rx;
    rx = (struct modbus_adu){.unit_id = 16, .fc = 16, .length = 1029};
    sys_put_be16(20049, rx.data); sys_put_be16(512, rx.data + 2);
    rx.data[1028] = 0xa5;
    zassert_ok(tr_register_attach(server, &service));
    atomic_clear(&extension_calls);
    capture_only = true;
    k_sem_reset(&captured);
    zassert_ok(modbus_raw_submit_rx(server, &rx));
    zassert_ok(k_sem_take(&captured, K_MSEC(100)));
    zassert_equal(captured_adu.fc, 16); zassert_equal(captured_adu.length, 4);
    rx.unit_id = 0;
    zassert_ok(modbus_raw_submit_rx(server, &rx));
    zassert_equal(k_sem_take(&captured, K_MSEC(100)), -EAGAIN);
    zassert_equal(atomic_get(&extension_calls), 2);
    rx.unit_id = 247;
    zassert_ok(modbus_raw_submit_rx(server, &rx));
    zassert_equal(k_sem_take(&captured, K_MSEC(100)), -EAGAIN);
    zassert_equal(atomic_get(&extension_calls), 2);
    /* Reattaching the product service removes the extension and its policy. */
    zassert_ok(attach_profile());
    rx.unit_id = 16;
    zassert_ok(modbus_raw_submit_rx(server, &rx));
    zassert_ok(k_sem_take(&captured, K_MSEC(100)));
    zassert_equal(captured_adu.fc, 16 | 0x80);
    zassert_equal(captured_adu.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
    capture_only = false;
}

ZTEST_SUITE(forwarding, NULL, setup, before, NULL, NULL);
