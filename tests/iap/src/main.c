#include <iap/iap.h>
#include <iap/sgz.h>
#include "iap_keys.h"
#include "fixture.h"
#include <zephyr/modbus/modbus.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>
#if defined(CONFIG_TR_REGISTER_SERVICE)
#include <transmit/registers.h>

static const MB_TABLE_SEG_T upgrade_segments[] = {
    {.reg_addr = 20003, .end_addr = 20003, .func = iap_route},
    {.reg_addr = 20049, .end_addr = 20104, .func = iap_route},
    {.reg_addr = 31996, .end_addr = 31997, .func = iap_route},
};
static const DEV_TABLE_S upgrade_device = {.dev_id = 1, .ptl_tbl_cfg = {
    [2] = {.proto_type = PRO_ADDR_SLAVE, .sig_num = ARRAY_SIZE(upgrade_segments),
           .pst_table = upgrade_segments},
}};
static const struct tr_register_service upgrade_service = {
    .device = &upgrade_device, .route_broadcast = true,
};
#endif

/* The routed variant exercises the real IAP state machine through TR dispatch;
 * the standalone variants retain the transport-independent receiver contract. */
static int dispatch(int iface, const struct modbus_adu *request, struct modbus_adu *response)
{
#if defined(CONFIG_TR_REGISTER_SERVICE)
    return tr_register_route(iface, request, response, (void *)&upgrade_service);
#else
    return iap_route(iface, request, response);
#endif
}

static struct modbus_adu rx, tx;
static bool confirmed = true;
static int freeze_error, pending_error, pending_calls, fail_writes;
static bool frozen;
static int hook_context;
static K_SEM_DEFINE(rebooted, 0, 1);
static const struct flash_area *stage, *slot, *primary;
static const char *phase;
bool __wrap_boot_is_img_confirmed(void) { return confirmed; }
int __real_boot_request_upgrade(int permanent);
int __wrap_boot_request_upgrade(int permanent)
{
    zassert_equal(permanent, BOOT_UPGRADE_TEST); ++pending_calls;
    return pending_error ? pending_error : __real_boot_request_upgrade(permanent);
}
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type); k_sem_give(&rebooted);
    for (;;) { k_sleep(K_FOREVER); }
}
static int prepare_reboot(void *user)
{
    zassert_equal(user, &hook_context);
    if (!freeze_error) { frozen = true; }
    return freeze_error;
}
static void cancel_reboot(void *user) { zassert_equal(user, &hook_context); frozen = false; }
static int inject_write(const struct device *dev, off_t off, uint8_t byte)
{ ARG_UNUSED(dev); ARG_UNUSED(off); return fail_writes ? -EIO : byte; }
static const struct flash_simulator_cb flash_cb = {.write_byte = inject_write};

static int command(uint16_t reg, uint16_t value)
{
    rx = (struct modbus_adu){.unit_id = 1, .fc = 6, .length = 4};
    sys_put_be16(reg, rx.data); sys_put_be16(value, rx.data + 2);
    return dispatch(2, &rx, &tx);
}
static int read_regs(uint16_t reg, uint16_t count)
{
    rx = (struct modbus_adu){.unit_id = 1, .fc = 3, .length = 4};
    sys_put_be16(reg, rx.data); sys_put_be16(count, rx.data + 2);
    return dispatch(2, &rx, &tx);
}
static uint16_t word(uint16_t reg)
{ zassert_ok(read_regs(reg, 1)); return sys_get_be16(tx.data + 1); }
static uint8_t state(void)
{ zassert_ok(read_regs(20063, 8)); return tx.data[16]; }
static uint8_t cause(void)
{ zassert_ok(read_regs(20071, 8)); return tx.data[1]; }
static void wait_state(int expected)
{
    for (int i = 0; i < 2000; ++i) {
        if (state() == expected) { k_sleep(K_MSEC(5)); return; }
        k_sleep(K_MSEC(1));
    }
    zassert_equal(state(), expected, "cause=%u", cause());
}
static void login(void)
{
    for (int i = 0; i < 100; ++i) {
        if (!command(20083, 0xaa)) { k_sleep(K_MSEC(10)); return; }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("login rejected");
}
static void configure(void)
{
    zassert_ok(command(20088, 1024));
    zassert_ok(command(20089, DIV_ROUND_UP(sizeof(fixture_sgz), 1024)));
}
static int send_blob(unsigned number, bool broadcast, bool corrupt, const uint8_t *blob)
{
    size_t every = word(20088);
    /* Deliberately malformed zero-size metadata is exercised by test_a. */
    if (!every) { every = 1024; }
    size_t offset = (number - 1u) * every;
    size_t length = MIN(every, sizeof(fixture_sgz) - offset);
    rx = (struct modbus_adu){.unit_id = broadcast ? 0 : 1, .fc = 16, .length = 13 + length};
    sys_put_be16(20090, rx.data); sys_put_be16(4 + length / 2, rx.data + 2);
    rx.data[4] = (uint8_t)(8 + length);
    sys_put_be16(number, rx.data + 5); sys_put_be16(offset, rx.data + 7);
    sys_put_be16(offset >> 16, rx.data + 9);
    sys_put_be16(offset + length == sizeof(fixture_sgz) ? length : 0, rx.data + 11);
    memcpy(rx.data + 13, blob + offset, length);
    if (corrupt) { rx.data[13 + length - 1] ^= 1; }
    return dispatch(2, &rx, &tx);
}
static int packet_send(unsigned number, bool broadcast, bool corrupt)
{ return send_blob(number, broadcast, corrupt, fixture_sgz); }
static void full_receive(void)
{
    login(); configure();
    for (unsigned i = 1; i <= DIV_ROUND_UP(sizeof(fixture_sgz), 1024); ++i) {
        zassert_ok(packet_send(i, false, false));
        k_sleep(K_MSEC(10)); zassert_equal(state(), 2);
    }
}
static void *setup(void)
{
#if defined(CONFIG_TR_REGISTER_SERVICE)
    zassert_ok(tr_register_attach(modbus_iface_get_by_name("RAW_2"), &upgrade_service));
#endif
    phase = getenv("IDU_IAP_PHASE");
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(iap_staging)), &stage));
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition)), &slot));
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)), &primary));
    flash_simulator_set_callbacks(flash_area_get_device(stage), &flash_cb);
    if (phase && !strcmp(phase, "confirmed")) {
        zassert_ok(flash_area_erase(primary, 0, primary->fa_size));
        /* Model the bootloader's completed swap; receiver still verifies flash. */
        uint8_t chunk[256];
        for (size_t off = 0; off < sizeof(fixture_image); off += sizeof(chunk)) {
            size_t n = MIN(sizeof(chunk), sizeof(fixture_image) - off);
            zassert_ok(flash_area_read(slot, off, chunk, n));
            zassert_ok(flash_area_write(primary, off, chunk, n));
        }
    }
    struct iap_config config = {
        .staging_area_id = stage->fa_id,
        .journal_area_id = DT_FIXED_PARTITION_ID(DT_NODELABEL(iap_journal)),
        .aes_key = iap_aes_key,
        .image = {0x6e000, 0x08010000, 0x20000000, 0x20020000,
                  iap_public_key, iap_key_hash},
        .prepare_reboot = prepare_reboot, .cancel_reboot = cancel_reboot,
        .user = &hook_context,
    };
    zassert_equal(iap_init_config(NULL), -EINVAL);
    struct iap_config bad = config;
    bad.cancel_reboot = NULL;
    zassert_equal(iap_init_config(&bad), -EINVAL);
    bad = config; bad.journal_area_id = bad.staging_area_id;
    zassert_equal(iap_init_config(&bad), -EINVAL);
    bad = config; bad.staging_area_id = 255;
    zassert_true(iap_init_config(&bad) < 0);
    bad = config; bad.image.max_size = slot->fa_size + 1;
    zassert_equal(iap_init_config(&bad), -EINVAL);
    zassert_false(iap_ready());
#ifdef CONFIG_IAP_CONFIGURED
    zassert_ok(iap_init());
#else
    zassert_ok(iap_init_config(&config));
#endif
    zassert_true(iap_ready());
    zassert_equal(iap_init_config(&config), -EALREADY);
    /* Caller may discard its configuration after startup. */
    memset(&config, 0, sizeof(config));
    return NULL;
}

ZTEST(iap, test_a_metadata_and_validation)
{
    if (phase) { ztest_test_skip(); }
    zassert_equal(iap_route(-1, &rx, &tx), -EINVAL);
    zassert_equal(iap_route(0, NULL, &tx), -EINVAL);
    zassert_equal(iap_route(0, &rx, NULL), -EINVAL);
    zassert_ok(read_regs(20003, 1));
    zassert_equal(sys_get_be16(tx.data + 1), 4);
    zassert_ok(read_regs(31996, 2));
    zassert_equal(sys_get_be16(tx.data + 1), 0x1200);
    zassert_equal(sys_get_be16(tx.data + 3), 0);
    zassert_ok(read_regs(31997, 1));
    zassert_equal(sys_get_be16(tx.data + 1), 0);
    zassert_equal(read_regs(31996, 3), IAP_ROUTE_SILENT);
    zassert_equal(command(20003, 1), IAP_ROUTE_SILENT);
    zassert_equal(read_regs(31000, 1), -ENOENT);
    zassert_ok(read_regs(20049, 2));
    zassert_equal(sys_get_be16(tx.data + 1), 0); zassert_equal(sys_get_be16(tx.data + 3), 0x0300);
    confirmed = false; zassert_equal(command(20083, 0xaa), IAP_ROUTE_SILENT); confirmed = true;
    login(); configure();
    zassert_equal(command(20088, 0), 0); /* incomplete metadata allowed; no packet accepted */
    zassert_equal(packet_send(1, false, false), IAP_ROUTE_SILENT);
    configure();
    zassert_equal(command(20088, 1025), IAP_ROUTE_SILENT); zassert_equal(word(20088), 1024);
    zassert_equal(command(20089, 0xffff), IAP_ROUTE_SILENT);
    zassert_equal(command(20090, 99), IAP_ROUTE_SILENT);
    zassert_equal(command(20091, 1), IAP_ROUTE_SILENT); /* partial u32 */
    zassert_equal(command(20083, 0xa1), IAP_ROUTE_SILENT);
    zassert_ok(read_regs(20099, 4)); zassert_equal(sys_get_be16(tx.data + 1), 1);
    zassert_equal(sys_get_be16(tx.data + 7), 1024);
    rx.unit_id = 247; zassert_equal(dispatch(2, &rx, &tx), IAP_ROUTE_SILENT);
    rx.unit_id = 1; zassert_equal(dispatch(3, &rx, &tx), IAP_ROUTE_SILENT);
    rx.unit_id = 0; rx.fc = 4; zassert_ok(dispatch(2, &rx, &tx));
    /* Combined login + reserved gap + packet geometry, nonstandard byte count. */
    rx = (struct modbus_adu){.unit_id = 1, .fc = 16, .length = 19};
    sys_put_be16(20083, rx.data); sys_put_be16(7, rx.data + 2); rx.data[4] = 0;
    sys_put_be16(0xaa, rx.data + 5); sys_put_be16(1024, rx.data + 15);
    sys_put_be16(DIV_ROUND_UP(sizeof(fixture_sgz), 1024), rx.data + 17);
    zassert_ok(dispatch(2, &rx, &tx)); k_sleep(K_MSEC(10));
    zassert_equal(word(20088), 1024); zassert_equal(state(), 1);
    zassert_ok(read_regs(20063, 8)); zassert_equal(tx.data[1], 0);
}

ZTEST(iap, test_b_out_of_order_duplicates_and_broadcast)
{
    if (phase) { ztest_test_skip(); }
    login(); configure();
    unsigned last = DIV_ROUND_UP(sizeof(fixture_sgz), 1024);
    zassert_equal(packet_send(last, true, false), IAP_ROUTE_SILENT); k_sleep(K_MSEC(10));
    zassert_equal(word(20103), last - 1); zassert_equal(word(20099), 1);
    zassert_ok(packet_send(last, false, false)); k_sleep(K_MSEC(10));
    zassert_equal(word(20103), last - 1);
    zassert_ok(packet_send(1, false, false)); k_sleep(K_MSEC(10));
    zassert_equal(word(20099), 2);
    zassert_equal(command(20088, 512), IAP_ROUTE_SILENT);
    zassert_ok(packet_send(1, false, true)); wait_state(6); zassert_equal(cause(), 3);
}

ZTEST(iap, test_b_combined_login_and_packet)
{
    if (phase) { ztest_test_skip(); }
    rx = (struct modbus_adu){.unit_id = 1, .fc = 16, .length = 1051};
    sys_put_be16(20083, rx.data); sys_put_be16(523, rx.data + 2); rx.data[4] = 22;
    sys_put_be16(0xaa, rx.data + 5); sys_put_be16(1024, rx.data + 15);
    sys_put_be16(DIV_ROUND_UP(sizeof(fixture_sgz), 1024), rx.data + 17);
    sys_put_be16(1, rx.data + 19); sys_put_be16(1024, rx.data + 25);
    memcpy(rx.data + 27, fixture_sgz, 1024);
    zassert_ok(dispatch(2, &rx, &tx)); k_sleep(K_MSEC(10));
    zassert_equal(state(), 2); zassert_equal(word(20099), 2);
    zassert_ok(command(20083, 0x55)); k_sleep(K_MSEC(10)); zassert_equal(state(), 0);
}

ZTEST(iap, test_c_flash_failure_and_timeout)
{
    if (phase) { ztest_test_skip(); }
    login(); configure(); fail_writes = 1;
    zassert_ok(packet_send(1, false, false)); wait_state(6); fail_writes = 0;
    zassert_equal(cause(), 4); zassert_equal(word(20103), DIV_ROUND_UP(sizeof(fixture_sgz), 1024));
    login(); k_sleep(K_MSEC(481000)); wait_state(6); zassert_equal(cause(), 6);
}

ZTEST(iap, test_c_sector_crossing_and_atomic_frame_rejection)
{
    if (phase) { ztest_test_skip(); }
    login(); zassert_ok(command(20088, 240));
    zassert_ok(command(20089, DIV_ROUND_UP(sizeof(fixture_sgz), 240)));
    zassert_ok(packet_send(18, false, false)); k_sleep(K_MSEC(10));
    zassert_ok(packet_send(17, false, false)); k_sleep(K_MSEC(10));
    zassert_ok(packet_send(18, false, false)); k_sleep(K_MSEC(10));
    zassert_equal(state(), 2); /* packet 17 must not erase packet 18's sector */
    zassert_equal(word(20103), DIV_ROUND_UP(sizeof(fixture_sgz), 240) - 2);
    rx = (struct modbus_adu){.unit_id = 1, .fc = 16, .length = 13};
    sys_put_be16(20090, rx.data); sys_put_be16(4, rx.data + 2); rx.data[4] = 8;
    sys_put_be16(1, rx.data + 5); sys_put_be16(240, rx.data + 11);
    /* Metadata can be written separately, as in the old protocol. */
    zassert_equal(dispatch(2, &rx, &tx), 0); /* metadata-only is valid */
    rx.length = 15; sys_put_be16(5, rx.data + 2); rx.data[4] = 10;
    sys_put_be16(2, rx.data + 5); /* claims 240 bytes but contains only 2 */
    zassert_equal(dispatch(2, &rx, &tx), IAP_ROUTE_SILENT);
    zassert_equal(word(20090), 1);
}

ZTEST(iap, test_d_save_and_boot_request_failures)
{
    if (phase) { ztest_test_skip(); }
#ifndef CONFIG_IAP_CONFIGURED
    freeze_error = -EBUSY; full_receive();
    zassert_equal(word(20103), 0); wait_state(6); zassert_equal(pending_calls, 0); zassert_false(frozen);
#endif
#ifndef CONFIG_IAP_CONFIGURED
    freeze_error = -EROFS; full_receive();
    zassert_equal(word(20103), 0); wait_state(6); zassert_equal(pending_calls, 0);
    zassert_false(frozen); zassert_equal(k_sem_take(&rebooted, K_NO_WAIT), -EBUSY);
#endif
    freeze_error = 0; pending_error = -EIO; full_receive();
    zassert_equal(word(20103), 0); wait_state(6); zassert_equal(pending_calls, 1); zassert_false(frozen);
    pending_error = 0;
}

ZTEST(iap, test_reboot_journal)
{
    if (!phase) { ztest_test_skip(); }
    if (!strcmp(phase, "receive")) { login(); configure(); zassert_ok(packet_send(1, false, false)); k_sleep(K_MSEC(10)); }
    else if (!strcmp(phase, "recover") || !strcmp(phase, "rollback")) { wait_state(6); }
    else if (!strcmp(phase, "confirmed") || !strcmp(phase, "confirmed_again")) { wait_state(5); }
    else if (!strcmp(phase, "pending")) {
        full_receive(); zassert_equal(word(20103), 0); zassert_ok(k_sem_take(&rebooted, K_SECONDS(3)));
    } else { zassert_unreachable("unknown phase"); }
}

ZTEST(iap, test_e_valid_sgz_with_invalid_signature)
{
    if (phase) { ztest_test_skip(); }
    login(); configure();
    for (unsigned i = 1; i <= DIV_ROUND_UP(sizeof(fixture_sgz), 1024); ++i) {
        zassert_ok(send_blob(i, false, false, fixture_bad_signature_sgz)); k_sleep(K_MSEC(10));
    }
    zassert_equal(word(20103), 0); wait_state(6); zassert_equal(cause(), 10);
    zassert_equal(pending_calls, 1); zassert_false(frozen);
}

ZTEST(iap, test_z_signed_image_staged_and_test_boot)
{
    if (phase) { ztest_test_skip(); }
    full_receive(); zassert_equal(pending_calls, 1);
    zassert_equal(word(20103), 0);
    zassert_ok(k_sem_take(&rebooted, K_SECONDS(3)));
    zassert_equal(state(), 7); zassert_equal(pending_calls, 2);
    zassert_equal(frozen, !IS_ENABLED(CONFIG_IAP_CONFIGURED));
    zassert_equal(mcuboot_swap_type(), BOOT_SWAP_TYPE_TEST);
    uint8_t data[128];
    for (size_t off = 0; off < sizeof(fixture_image); off += sizeof(data)) {
        size_t n = MIN(sizeof(data), sizeof(fixture_image) - off);
        zassert_ok(flash_area_read(slot, off, data, n));
        zassert_mem_equal(data, fixture_image + off, n);
    }
    /* The running image and old configuration gap were never written. */
    zassert_ok(flash_area_read(primary, 0, data, sizeof(data)));
    for (size_t i = 0; i < sizeof(data); ++i) { zassert_equal(data[i], 0xff); }
    zassert_equal(command(20083, 0xaa), IAP_ROUTE_SILENT);
}
ZTEST_SUITE(iap, NULL, setup, NULL, NULL, NULL);
