#include "idu_equipment.h"
#include "cfg/devices/device_config.h"
#include "cfg/tables/signal_ids.h"
#include "communication/idu_protocol.h"
#include "communication/config/protocol_config.h"
#include "communication/ports/idu_acquisition.h"
#include "communication/ports/idu_virtual.h"
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <dataprocess/points.h>
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
static const struct flash_area *storage, *primary, *backup;
static struct modbus_iface_param server_param;
static int fail_after = -1;
static K_SEM_DEFINE(config_done, 0, 1);
static int config_error;
static bool capture_only;
static K_SEM_DEFINE(captured, 0, 1);

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
    if (capture_only) { k_sem_give(&captured); return 0; }
    return modbus_raw_submit_rx(client, adu);
}
static void completed(int iface, int err, void *user)
{
    ARG_UNUSED(user); zassert_equal(iface, server);
    config_error = err; k_sem_give(&config_done);
}

static void legacy_record(const struct flash_area *area, uint8_t address, bool corrupt, size_t len)
{
    uint8_t record[9] = {address, 0, 0x21, 0x22, 1, 1, 0, 1, 0};
    uint8_t metadata[4];
    zassert_true(len <= sizeof(record));
    sys_put_le16(len, metadata);
    sys_put_le16(crc16_ansi(record, len) ^ (corrupt ? 1 : 0), metadata + 2);
    if (address == 37 && len == 8 && !corrupt) { zassert_equal(sys_get_le16(metadata + 2), 0x66ed); }
    zassert_ok(flash_area_erase(area, 0, area->fa_size));
    zassert_ok(flash_area_write(area, 0, record, len));
    zassert_ok(flash_area_write(area, 4092, metadata, sizeof(metadata)));
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
static const uint8_t old_snapshot[] = {
    0x49,0x44,0x55,0x50,0x01,0x00,0x04,0x00,0x38,0xe9,0x1b,0xfa,0x2a,0x00,0x00,0x00,
    0x01,0x00,0x18,0x79,0x00,0x01,0xee,0x02,0x01,0x00,0x19,0x79,0x04,0x08,0x42,0x41,
    0x00,0x43,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,
    0x0c,0x7b,0x00,0x01,0xbc,0x02,0x0a,0x00,0x0d,0x7b,0x03,0x02,0xf4,0xff,0xff,0xff,
};
BUILD_ASSERT(sizeof(old_snapshot) == 64);

static void write_old_fixture(bool intent)
{
    fresh_nvs();
    struct nvs_fs fs;
    mount_fixture(&fs);
    zassert_equal(nvs_write(&fs, 1, old_snapshot, sizeof(old_snapshot)), sizeof(old_snapshot));
    if (intent) {
        uint8_t pending[88] = {0};
        sys_put_le32(0x4a554449, pending); sys_put_le16(1, pending + 4);
        pending[6] = 1; pending[7] = 1; sys_put_le16(40000, pending + 8);
        sys_put_le32(43, pending + 12); sys_put_le16(64, pending + 16); sys_put_le16(10, pending + 18);
        sys_put_le16(700, pending + 20); sys_put_le16(888, pending + 22);
        memcpy(pending + 24, old_snapshot, sizeof(old_snapshot));
        sys_put_le32(43, pending + 24 + 12); sys_put_le16(888, pending + 24 + 52);
        zassert_equal(nvs_write(&fs, 2, pending, sizeof(pending)), sizeof(pending));
    }
}

static int attach_profile(void)
{
    const struct idu_protocol_profile *profile = idu_protocol_config();

    return profile->attach ? profile->attach(server) : tr_attach(server, NULL);
}

static void *setup(void)
{
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)), &storage));
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_legacy_primary)), &primary));
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_legacy_backup)), &backup));
    flash_simulator_set_callbacks(flash_area_get_device(storage), &flash_cb);
    if (!getenv("IDU_MVIO_PHASE") || !strcmp(getenv("IDU_MVIO_PHASE"), "import")) {
        legacy_record(primary, 37, false, 8);
        legacy_record(backup, 41, false, 8);
    }
    const struct dp_equip_cfg *cfg = idu_equipment_config();
    zassert_ok(dp_init_config(cfg));
    int south = idu_acquisition_port_init(); zassert_true(south >= 0);
    zassert_ok(co_init(cfg, south, true));
    zassert_ok(dp_params_init(cfg, storage->fa_id));
    server = modbus_iface_get_by_name("RAW_2"); client = modbus_iface_get_by_name("RAW_3");
    server_param = (struct modbus_iface_param){.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = idu_parameter_callbacks()}, .rawcb = {.raw_tx_cb = response}};
    struct modbus_iface_param client_param = {.mode = MODBUS_MODE_RAW, .rx_timeout = 50000,
                                              .rawcb = {.raw_tx_cb = request}};
    zassert_ok(attach_profile());
    const struct idu_protocol_profile *profile = idu_protocol_config();
    if (profile->serial) { zassert_ok(profile->serial(&server_param)); }
    zassert_ok(tr_modbus_init_server(server, server_param, idu_parameter_write, true));
    zassert_ok(modbus_init_client(client, client_param));
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture); fail_after = -1; fail_init = 0;
    if (getenv("IDU_MVIO_PHASE")) { return; }
    legacy_record(primary, 37, false, 8); legacy_record(backup, 41, false, 8);
    fresh_nvs(); zassert_ok(dp_params_reload());
    zassert_ok(attach_profile());
    zassert_ok(idu_virtual_set_register(3, 40000, 100));
    struct dp_point_batch batch = {.source = DP_SOURCE_LOCAL, .count = 2,
        .points = {{.key = {1, IDU_SIG_ADDRESS}, .width = 1, .value.words = {16}},
                   {.key = {1, IDU_SIG_DI}, .width = 1, .value.words = {0xa55a}}}};
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    zassert_ok(dp_wait_processed(batch.ticket, K_MSEC(200)));
}

ZTEST(mvio, test_saved_address_and_247)
{
    uint16_t word;
    zassert_equal(saved(), 37);
    zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1)); zassert_equal(word, 37);
    zassert_ok(modbus_write_holding_reg(client, 247, 30459, 246));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 246);
    zassert_ok(modbus_read_input_regs(client, 16, 8152, &word, 1)); zassert_equal(word, 0xa55a);
    zassert_equal(modbus_read_holding_regs(client, 246, 30459, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_read_input_regs(client, 247, 8152, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_read_input_regs(client, 247, 30459, &word, 1), -ETIMEDOUT);
    zassert_equal(modbus_write_holding_reg(client, 247, 31000, 800), -ETIMEDOUT);
    /* Legacy invalid values ACK without applying; they do not set the DIP. */
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 0));
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 247)); zassert_equal(saved(), 246);
}

ZTEST(mvio, test_reserved_reads_noop_writes_and_table_walk)
{
    uint16_t words[12];
    zassert_ok(modbus_read_holding_regs(client, 16, 8149, words, 12));
    for (int i = 0; i < 12; ++i) { zassert_equal(words[i], 0); }
    /* 8158 advances across the gap to 8178, rather than reading 8159. */
    zassert_ok(modbus_read_holding_regs(client, 16, 8158, words, 2));
    zassert_equal(modbus_read_holding_regs(client, 16, 8159, words, 1), -ETIMEDOUT);
    zassert_ok(modbus_read_holding_regs(client, 16, 39278, words, 8));
    for (int i = 0; i < 8; ++i) { zassert_equal(words[i], 0); }
    zassert_ok(modbus_write_holding_reg(client, 16, 8159, 0xffff));
    zassert_ok(modbus_write_holding_reg(client, 16, 39281, 0));
    for (int i = 0; i < 12; ++i) { words[i] = 0x8000 + i; }
    zassert_ok(modbus_write_holding_regs(client, 16, 8178, words, 12));
    zassert_equal(saved(), 37);
    zassert_ok(modbus_read_input_regs(client, 16, 2532, words, 2));
    zassert_equal(words[0], 0); zassert_equal(words[1], 0x3031); /* next entry 2568 */
    zassert_equal(modbus_read_input_regs(client, 16, 8151, words, 1), -ETIMEDOUT);
    zassert_equal(modbus_read_input_regs(client, 16, 4814, words, 1), -ETIMEDOUT);
    zassert_equal(modbus_read_input_regs(client, 16, 2581, words, 1), -ETIMEDOUT);
}

ZTEST(mvio, test_broadcast_contract_and_extension_isolation)
{
    uint16_t word, value = 42;
    zassert_ok(modbus_read_input_regs(client, 0, 8149, &word, 1)); zassert_equal(word, 0x2221);
    zassert_ok(modbus_write_holding_regs(client, 0, 30459, &value, 1)); zassert_equal(saved(), 42);
    zassert_equal(modbus_write_holding_reg(client, 0, 30459, 43), -ETIMEDOUT);
    zassert_equal(modbus_read_holding_regs(client, 0, 30459, &word, 1), -ETIMEDOUT);
    value = 800;
    zassert_equal(modbus_write_holding_regs(client, 0, 31000, &value, 1), -ETIMEDOUT);
    zassert_equal(idu_sample_period_ms(), 500); zassert_equal(saved(), 42);
    zassert_ok(modbus_write_holding_reg(client, 16, 31000, 800));
    zassert_equal(idu_sample_period_ms(), 800);
    zassert_equal(modbus_write_holding_reg(client, 16, 31000, 0), MODBUS_EXC_ILLEGAL_DATA_VAL);
}

ZTEST(mvio, test_malformed_and_unsupported_requests_are_silent)
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
        zassert_equal(k_sem_take(&captured, K_MSEC(50)), -EAGAIN);
        zassert_equal(saved(), 37);
    }
    capture_only = false;
}

ZTEST(mvio, test_primary_backup_import_and_readonly_source)
{
    uint32_t p = checksum(primary), b = checksum(backup);
    zassert_equal(saved(), 37);
    zassert_ok(modbus_write_holding_reg(client, 16, 30459, 88));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 88);
    zassert_equal(checksum(primary), p); zassert_equal(checksum(backup), b);
    legacy_record(primary, 37, true, 8); fresh_nvs();
    p = checksum(primary); zassert_ok(dp_params_reload()); zassert_equal(saved(), 41);
    zassert_equal(checksum(primary), p); zassert_equal(checksum(backup), b);
    legacy_record(backup, 41, true, 8); fresh_nvs();
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 1);
    legacy_record(primary, 0, false, 8); fresh_nvs();
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 0);
    legacy_record(primary, 255, false, 8); fresh_nvs();
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 1);
}

ZTEST(mvio, test_unknown_legacy_layout_does_not_reset_valid_configuration)
{
    legacy_record(primary, 37, false, 9); fresh_nvs();
    zassert_equal(dp_params_reload(), -EPROTO);
    zassert_equal(dp_params_flush(), -EROFS);
    legacy_record(primary, 39, false, 8);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 39);
    /* Once NVS exists, old flash changes are irrelevant. */
    legacy_record(primary, 37, false, 9);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 39);
}

ZTEST(mvio, test_failed_address_save_has_no_success_ack)
{
    fail_after = 0;
    zassert_equal(modbus_write_holding_reg(client, 247, 30459, 83), -ETIMEDOUT);
    fail_after = -1;
    zassert_equal(saved(), 37);
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_false(status.flags & DP_PARAM_STORAGE_READY);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 37);
    zassert_ok(modbus_write_holding_reg(client, 247, 30459, 83));
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 83);
}

ZTEST(mvio, test_iap_is_not_advertised_or_written)
{
    uint16_t words[2];
    zassert_ok(modbus_read_holding_regs(client, 16, 20003, words, 1)); zassert_equal(words[0], 0);
    zassert_ok(modbus_read_holding_regs(client, 16, 31996, words, 2));
    zassert_equal(words[0], 0); zassert_equal(words[1], 0);
    zassert_equal(modbus_write_holding_reg(client, 16, 20049, 3), -ETIMEDOUT);
    zassert_equal(modbus_read_holding_regs(client, 16, 20049, words, 1), -ETIMEDOUT);
}

ZTEST(mvio, test_previous_zephyr_schema_preserves_all_values)
{
    write_old_fixture(false);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 37);
    zassert_equal(idu_sample_period_ms(), 750);
    uint16_t words[8];
    zassert_ok(modbus_read_holding_regs(client, 16, 31001, words, 8));
    zassert_equal(words[0], 0x4142); zassert_equal(words[1], 0x4300);
    zassert_ok(modbus_read_holding_regs(client, 16, 31500, words, 3));
    zassert_equal(words[0], 700); zassert_equal(words[1], 0xfff4); zassert_equal(words[2], 0xffff);
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_true(status.flags & DP_PARAM_SCHEMA_MIGRATED); zassert_equal(status.sequence, 42);
    legacy_record(primary, 99, false, 8);
    zassert_ok(dp_params_reload()); zassert_equal(saved(), 37); zassert_equal(idu_sample_period_ms(), 750);
}

ZTEST(mvio, test_old_pending_intent_is_readback_only)
{
    for (int applied = 0; applied < 2; ++applied) {
        write_old_fixture(true);
        uint16_t actual = applied ? 888 : 700;
        zassert_ok(idu_virtual_set_register(3, 40000, actual));
        unsigned writes = idu_virtual_write_count();
        zassert_ok(dp_params_reload());
        struct dp_param_status status; dp_params_get_status(&status);
        zassert_true(status.flags & DP_PARAM_PENDING);
        zassert_ok(dp_params_recover()); zassert_equal(idu_virtual_write_count(), writes);
        zassert_equal(saved(), 37); zassert_equal(idu_sample_period_ms(), 750);
        uint16_t word; zassert_ok(modbus_read_holding_regs(client, 16, 31500, &word, 1));
        zassert_equal(word, actual);
        zassert_ok(dp_params_reload()); zassert_equal(saved(), 37);
        dp_params_get_status(&status); zassert_false(status.flags & DP_PARAM_PENDING);
    }
}

ZTEST(mvio, test_import_torn_write_is_retryable)
{
    uint32_t p = checksum(primary), b = checksum(backup);
    for (int cut = 0; cut < 110; ++cut) {
        fresh_nvs(); fail_after = cut;
        int err = dp_params_reload(); fail_after = -1;
        zassert_true(err == 0 || err == -EIO, "cut=%d err=%d", cut, err);
        zassert_ok(dp_params_reload()); zassert_equal(saved(), 37);
    }
    zassert_equal(checksum(primary), p); zassert_equal(checksum(backup), b);
}

ZTEST(mvio, test_recovery_reinitializes_and_rolls_back)
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
            uint16_t word; zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1));
            zassert_equal(word, 37);
        } else { zassert_equal(tr_modbus_service(server, 20000), -ENOTRECOVERABLE); }
    }
    zassert_ok(tr_modbus_init_server(server, server_param, idu_parameter_write, true));
}

ZTEST(mvio, test_idle_service_recovers_without_resetting_parameters)
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
    zassert_equal(saved(), 37);
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1));
}

ZTEST(mvio, test_stuck_tx_state_can_be_forced_back_to_receive)
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
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1));
    zassert_equal(word, 37);
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

ZTEST(mvio, test_stuck_handler_has_bounded_recovery_and_watchdog_error)
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
    uint16_t word; zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1));
}

ZTEST(mvio, test_reboot_probe)
{
    const char *phase = getenv("IDU_MVIO_PHASE");
    if (!phase) { ztest_test_skip(); return; }
    if (!strcmp(phase, "import")) {
        zassert_equal(saved(), 37);
        zassert_ok(modbus_write_holding_reg(client, 247, 30459, 91));
    } else if (!strcmp(phase, "verify")) {
        zassert_equal(saved(), 91);
        uint16_t word; zassert_ok(modbus_read_holding_regs(client, 247, 30459, &word, 1));
        zassert_equal(word, 91);
    } else { zassert_unreachable("unknown phase"); }
}

ZTEST_SUITE(mvio, NULL, setup, before, NULL, NULL);
