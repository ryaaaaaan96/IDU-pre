#include <dataprocess/table_registry.h>
#include <dataprocess/params.h>
#include <platform_callback.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static const struct dp_device_binding bindings[] = {
    {.dev_id = 1, .coll_addr = 7,
     .tables = {{20, PRO_DATA_PROCESS}, {30, PRO_MB_SLAVE}, {10, PRO_MB_MASTER},
                {40, PRO_ADDR_SLAVE}}},
};
static DEV_TABLE_S devices[ARRAY_SIZE(bindings)];
static unsigned reads, writes, flash_writes;
static bool flash_failure;
static int write_error;
static uint16_t remote[3];
static const DEV_TABLE_S *get_devices(void) { return devices; }
static uint16_t get_count(void) { return ARRAY_SIZE(devices); }
static int read_words(void *user, uint16_t dev, uint16_t address, uint16_t *out, size_t count)
{
    ARG_UNUSED(user); zassert_equal(dev, 1);
    zassert_true(address >= 10 && address + count <= 13);
    ++reads;
    memcpy(out, remote + address - 10, count * 2);
    return 0;
}
static int write_words(void *user, uint16_t dev, uint16_t address, const uint16_t *in, size_t count)
{
    ARG_UNUSED(user); zassert_equal(dev, 1);
    zassert_true(address >= 10 && address + count <= 13);
    ++writes;
    if (write_error) { return write_error; }
    memcpy(remote + address - 10, in, count * 2);
    return 0;
}
static int flash_byte(const struct device *dev, off_t offset, uint8_t value)
{
    ARG_UNUSED(dev); ARG_UNUSED(offset); ARG_UNUSED(value);
    ++flash_writes;
    return flash_failure ? -EIO : value;
}
static const struct flash_simulator_cb flash_callbacks = {.write_byte = flash_byte};
static uint16_t value(uint16_t sig)
{
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){1, sig}, &state));
    return state.value.words[0];
}
static void *setup(void)
{
    zassert_ok(dp_table_registry_bind(bindings, ARRAY_SIZE(bindings), devices));
    pltcb_reg_cfg_get_dev_tbl(get_devices); pltcb_reg_cfg_get_dev_num(get_count);
    zassert_ok(dp_init());
    const struct dp_params_transport write_only = {.write = write_words};
    zassert_ok(dp_params_set_transport(&write_only));
    const struct dp_params_transport transport = {.read = read_words, .write = write_words};
    zassert_ok(dp_params_set_transport(&transport));
    const struct flash_area *area;
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), &area));
    zassert_ok(flash_area_erase(area, 0, area->fa_size));
    zassert_ok(dp_params_init(dp_config(), DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition))));
    flash_simulator_set_callbacks(flash_area_get_device(area), &flash_callbacks);
    return NULL;
}
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    flash_failure = false; write_error = 0;
    zassert_ok(dp_params_reload());
    reads = writes = flash_writes = 0;
}
ZTEST(table_registry, test_discovery_and_independent_polling)
{
    const struct dp_equip_cfg *cfg = dp_config();
    zassert_equal(cfg->point_count, 4);
    zassert_equal(cfg->collect_count, 2);
    zassert_equal(cfg->devices[0].unit_id, 7);
    struct co_cfg scratch;
    const struct co_cfg *poll = dp_config_collect(cfg, 0, &scratch);
    zassert_equal(poll->fields[0].sig_id, 3);
    zassert_equal(poll->period_ms, 100);
    poll = dp_config_collect(cfg, 1, &scratch);
    zassert_not_null(poll);
    zassert_equal(poll->fields[0].sig_id, 4);
    zassert_equal(poll->period_ms, 2000); /* Same device, independently configured interval. */
    zassert_equal(devices[0].ptl_tbl_cfg[1].proto_type, PRO_MB_SLAVE);
    /* 协议段不增加 DP 点数、轮询数或普通转发行数。 */
    zassert_equal(devices[0].ptl_tbl_cfg[3].proto_type, PRO_ADDR_SLAVE);
    zassert_equal(devices[0].ptl_tbl_cfg[3].sig_num, 1);
    zassert_not_null(devices[0].ptl_tbl_cfg[3].pst_table);
    zassert_equal(cfg->transmit_count, 4);
}
ZTEST(table_registry, test_invalid_bindings_leave_output_untouched)
{
    const uint16_t dummy = 0;
    struct dp_registered_table entries[] = {
        {.id = 20, .type = PRO_DATA_PROCESS, .rows = &dummy, .count = 1},
        {.id = 30, .type = PRO_MB_SLAVE, .rows = &dummy, .count = 1},
        {.id = 10, .type = PRO_MB_MASTER, .rows = &dummy, .count = 1},
        {.id = 40, .type = PRO_ADDR_SLAVE, .rows = &dummy, .count = 1},
    };
    DEV_TABLE_S output, original;
    memset(&original, 0xa5, sizeof(original)); output = original;
    entries[1].id = 20;
    zassert_equal(dp_table_bind(bindings, 1, entries, 3, &output), -EEXIST);
    zassert_mem_equal(&output, &original, sizeof(output));
    entries[1].id = 30;
    zassert_equal(dp_table_bind(bindings, 1, entries, 2, &output), -ENOENT);
    entries[1].type = PRO_MB_MASTER;
    zassert_equal(dp_table_bind(bindings, 1, entries, 4, &output), -EINVAL);
    zassert_mem_equal(&output, &original, sizeof(output));
    entries[1].type = PRO_MB_SLAVE;
    entries[1].count = UINT16_MAX + 1U;
    zassert_equal(dp_table_bind(bindings, 1, entries, 4, &output), -EINVAL);
    entries[1].count = 1;
    entries[3].type = PRO_MB_SLAVE;
    zassert_equal(dp_table_bind(bindings, 1, entries, 4, &output), -EINVAL);
    zassert_mem_equal(&output, &original, sizeof(output));
}
ZTEST(table_registry, test_volatile_command_does_not_read_or_store)
{
    uint16_t command = 23;
    zassert_ok(dp_params_write_registers(dp_config(), 200, &command, 1));
    zassert_equal(writes, 1); zassert_equal(reads, 0); zassert_equal(flash_writes, 0);
    zassert_equal(value(2), command);
    zassert_ok(dp_params_write_registers(dp_config(), 200, &command, 1));
    zassert_equal(writes, 2); /* Identical commands still execute once per request. */
    zassert_ok(dp_params_reload());
    zassert_equal(value(2), 0); /* Not restored from NVS. */
    zassert_equal(writes, 2);
}
ZTEST(table_registry, test_failed_command_preserves_cache_without_replay)
{
    uint16_t command = 44, old = value(2), remote_before = remote[0];
    write_error = -ETIMEDOUT;
    zassert_equal(dp_params_write_registers(dp_config(), 200, &command, 1), -ETIMEDOUT);
    zassert_equal(value(2), old);
    zassert_equal(writes, 1); zassert_equal(reads, 0); zassert_equal(flash_writes, 0);
    write_error = 0;
    zassert_ok(dp_params_recover());
    zassert_equal(writes, 1);
    zassert_ok(dp_params_sync_device(1));
    zassert_equal(remote[0], remote_before); /* Sync never replays volatile command registers. */
}
ZTEST(table_registry, test_poll_feedback_neither_writes_nor_persists)
{
    k_sleep(K_MSEC(1));
    struct dp_point_batch batch = {.source = DP_SOURCE_MODBUS, .count = 1,
        .points = {{.key = {1, 3}, .width = 1, .value.words = {59}}}};
    zassert_ok(dp_publish(&batch, K_MSEC(100)));
    zassert_ok(dp_wait_processed(batch.ticket, K_SECONDS(1)));
    zassert_equal(value(3), 59);
    zassert_equal(writes, 0); zassert_equal(flash_writes, 0);
    uint16_t command = 61;
    zassert_ok(dp_params_write_registers(dp_config(), 201, &command, 1));
    zassert_equal(remote[1], 61);
    zassert_ok(dp_params_sample(batch.points, 1, 0, batch.sampled_at));
    zassert_equal(value(3), 61); /* An old queued sample cannot undo a write. */
}
ZTEST(table_registry, test_persistent_feedback_does_not_replace_saved_setting)
{
    uint16_t setting = 72;
    zassert_ok(dp_params_write_registers(dp_config(), 202, &setting, 1));
    zassert_true(flash_writes > 0); zassert_true(reads >= 2);
    k_sleep(K_MSEC(1));
    struct dp_point_update sample = {.key = {1, 4}, .width = 1, .value.words = {73}};
    unsigned saved_writes = flash_writes;
    zassert_ok(dp_params_sample(&sample, 1, 0, k_uptime_get()));
    zassert_equal(value(4), 73); zassert_equal(flash_writes, saved_writes);
    zassert_ok(dp_params_sync_device(1));
    zassert_equal(remote[2], 72); /* Explicit sync sends the desired, not measured value. */
    zassert_equal(value(4), 73);
    zassert_ok(dp_params_reload());
    zassert_equal(value(4), 72);
}
ZTEST(table_registry, test_mixed_durability_rejected_before_io)
{
    uint16_t mixed[] = {81, 82};
    zassert_equal(dp_params_write_registers(dp_config(), 201, mixed, 2), -EXDEV);
    zassert_equal(writes, 0); zassert_equal(reads, 0); zassert_equal(flash_writes, 0);
}
ZTEST(table_registry, test_volatile_write_survives_storage_failure)
{
    uint16_t setting = value(1) + 1;
    flash_failure = true;
    zassert_not_equal(dp_params_write_registers(dp_config(), 100, &setting, 1), 0);
    flash_writes = 0;
    zassert_ok(dp_params_write_registers(dp_config(), 200, &setting, 1));
    zassert_equal(flash_writes, 0); zassert_equal(writes, 1);
    zassert_equal(dp_params_flush(), -EROFS);
    flash_failure = false;
}
ZTEST(table_registry, test_failed_poll_preserves_value_and_reports_quality)
{
    k_sleep(K_MSEC(1));
    uint16_t old = value(3);
    struct dp_point_batch batch = {.source = DP_SOURCE_MODBUS, .count = 1,
        .points = {{.key = {1, 3}, .width = 1, .error = -ETIMEDOUT}}};
    zassert_ok(dp_publish(&batch, K_MSEC(100)));
    zassert_ok(dp_wait_processed(batch.ticket, K_SECONDS(1)));
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){1, 3}, &state));
    zassert_equal(state.value.words[0], old);
    zassert_equal(state.error, -ETIMEDOUT);
    zassert_equal(state.quality, DP_QUALITY_COMM_ERROR);
    zassert_equal(writes, 0); zassert_equal(flash_writes, 0);
}
ZTEST_SUITE(table_registry, NULL, setup, before, NULL, NULL);

ZTEST(table_registry, test_data_device_bindings_reject_invalid_references)
{
    struct data_device_cfg cfg = {.dev_id = 10, .process_table = 20,
        .collect_table = 10, .collect_port = 0, .collect_addr = 1};
    DEV_TABLE_S view, original;
    memset(&original, 0xa5, sizeof(original)); view = original;
    cfg.process_table = 999;
    zassert_equal(dp_data_device_bind(&cfg, 1, &view), -ENOENT);
    zassert_mem_equal(&view, &original, sizeof(view));
    cfg.process_table = 30;
    zassert_equal(dp_data_device_bind(&cfg, 1, &view), -EINVAL);
    cfg.process_table = 20; cfg.collect_addr = 248;
    zassert_equal(dp_data_device_bind(&cfg, 1, &view), -EINVAL);
    cfg.collect_table = 0; /* Non-acquiring devices ignore both collect fields. */
    zassert_ok(dp_data_device_bind(&cfg, 1, &view));
    struct data_device_cfg duplicate[] = {cfg, cfg};
    DEV_TABLE_S pair[2];
    zassert_equal(dp_data_device_bind(duplicate, 2, pair), -EEXIST);
}
