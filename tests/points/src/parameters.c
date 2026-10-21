#include <dataprocess/point_parameters.h>
#include <platform_callback.h>
#include "simulation.h"
#include "protocol/inc/protocol.h"
#include <collect/simulator.h>
#include <transmit/registers.h>
#include "process/idu_process.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/params.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

extern int north_client;
static int bytes_before_failure = -1;
static bool fail_after_downstream;
static unsigned writes_at_start;
static const struct flash_area *storage;

static int inject_failure(const struct device *dev, off_t offset, uint8_t data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(offset);
    if ((fail_after_downstream && co_simulator_write_count(&idu_collect_simulator) > writes_at_start) ||
        bytes_before_failure == 0) {
        return -EIO;
    }
    if (bytes_before_failure > 0) {
        --bytes_before_failure;
    }
    return data;
}

static const struct flash_simulator_cb flash_callbacks = {.write_byte = inject_failure};

static struct dp_param_status param_status(void)
{
    struct dp_param_status s;
    dp_params_get_status(&s);
    return s;
}

static uint16_t read_word(uint16_t address)
{
    uint16_t value;
    zassert_ok(modbus_read_holding_regs(north_client, 16, address, &value, 1));
    return value;
}

void parameter_before(void)
{
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)), &storage));
    flash_simulator_set_callbacks(flash_area_get_device(storage), &flash_callbacks);
    bytes_before_failure = -1;
    fail_after_downstream = false;
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_OK);
    co_simulator_set_online(&idu_collect_simulator, true);
    if (getenv("IDU_REBOOT_PHASE")) {
        return; /* A dedicated process run must inspect the boot-loaded state. */
    }
    zassert_ok(dp_params_reload());
    zassert_ok(dp_params_recover());
    zassert_ok(dp_params_restore(IDU_DEV_LOCAL));
    zassert_ok(dp_params_restore(IDU_DEV_REMOTE_IO));
    zassert_ok(dp_params_restore(20));
}

ZTEST(points, test_country_parameter_lookup_is_read_only)
{
    DM_SIG_DATA_S data;
    bool restore;

    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    uint32_t sequence = param_status().sequence;
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON,
        IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD, &data, &restore));
    zassert_equal(data.def_value.u16, CONFIG_IDU_SAMPLE_PERIOD_MS);
    zassert_equal(data.min_value.u16, 100);
    zassert_equal(data.max_value.u16, 1000);
    zassert_true(restore);
    bool recover = true;
    zassert_ok(pltcb_para_get_range_def(IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD, &data, &recover));
    zassert_false(recover);
    zassert_equal(read_word(31000), 750);
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31000), 750);
    zassert_equal(param_status().sequence, sequence);

    /* 型号参数保留有符号范围，数组初值保持原字序。 */
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON,
        IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_OFFSET, &data, &restore));
    zassert_equal(data.def_value.s32, 0);
    zassert_equal(data.min_value.s32, -100000);
    zassert_equal(data.max_value.s32, 100000);
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON,
        IDU_DEV_LOCAL, IDU_SIG_LABEL, &data, &restore));
    const uint16_t label[] = {0x4944, 0x552d, 0x7072, 0x6500, 0, 0, 0, 0};
    zassert_mem_equal(data.def_value.pst, label, sizeof(label));
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON,
        IDU_DEV_LOCAL, IDU_SIG_DI, &data, &restore));
    zassert_equal(data.def_value.u16, 0);
    zassert_equal(data.min_value.u16, 0);
    zassert_equal(data.max_value.u16, 0);
}

ZTEST(points, test_country_parameter_lookup_rejects_unknown_keys)
{
    DM_SIG_DATA_S data = {.def_value.u32 = 42, .min_value.u32 = 1, .max_value.u32 = 99};
    DM_SIG_DATA_S original = data;
    bool restore = false;

    zassert_equal(dp_point_parameters_get(UINT16_MAX, IDU_DEV_LOCAL,
        IDU_SIG_SAMPLE_PERIOD, &data, &restore), -ENOTSUP);
    zassert_equal(dp_point_parameters_get(DP_COUNTRY_COMMON, 257,
        IDU_SIG_SAMPLE_PERIOD, &data, &restore), -ENOENT);
    zassert_equal(dp_point_parameters_get(DP_COUNTRY_COMMON, IDU_DEV_LOCAL,
        UINT16_MAX, &data, &restore), -ENOENT);
    zassert_equal(dp_point_parameters_get(DP_COUNTRY_COMMON, IDU_DEV_LOCAL,
        IDU_SIG_SAMPLE_PERIOD, NULL, &restore), -EINVAL);
    zassert_equal(dp_point_parameters_get(DP_COUNTRY_COMMON, IDU_DEV_LOCAL,
        IDU_SIG_SAMPLE_PERIOD, &data, NULL), -EINVAL);
    zassert_equal(data.def_value.u32, original.def_value.u32);
    zassert_equal(data.min_value.u32, original.min_value.u32);
    zassert_equal(data.max_value.u32, original.max_value.u32);
    zassert_false(restore);
}

ZTEST(points, test_parameters_local_write_persists_and_applies)
{
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    zassert_equal(idu_sample_period_ms(), 750);
    zassert_equal(read_word(31000), 750);
    uint32_t sequence = param_status().sequence;
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31000), 750);
    zassert_equal(param_status().sequence, sequence);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    zassert_equal(param_status().sequence, sequence); /* No change, no new generation. */
    struct dp_param_status event;
    zassert_ok(zbus_chan_read(&dp_param_events, &event, K_NO_WAIT));
    zassert_equal(event.sequence, sequence);
    zassert_ok(event.error);
    zassert_equal(read_word(32000), DP_PARAM_STORAGE_READY);
}

ZTEST(points, test_parameters_full_request_validated_before_write)
{
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    uint16_t bad_remote[3] = {321, 0xffff, 0x7fff}; /* Last S32 out of range. */
    zassert_equal(modbus_write_holding_regs(north_client, 16, 31500, bad_remote, 3),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count);
    zassert_equal(read_word(31500), 100);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31000, 99),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31501, 7),
                  MODBUS_EXC_ILLEGAL_DATA_VAL); /* Half of S32. */
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31001, 7),
                  MODBUS_EXC_ILLEGAL_DATA_VAL); /* Partial label. */
    uint16_t crossing[10] = {700}; /* First nine valid, tenth unmapped. */
    zassert_equal(modbus_write_holding_regs(north_client, 16, 31000, crossing, 10),
                  MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(read_word(31000), 500);
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31000), 500);
}

ZTEST(points, test_parameters_remote_multiword_round_trip)
{
    uint16_t request[3] = {321, (uint16_t)-1234, 0xffff};
    zassert_ok(modbus_write_holding_regs(north_client, 16, 31500, request, 3));
    uint16_t result[3];
    zassert_ok(modbus_read_holding_regs(north_client, 16, 31500, result, 3));
    zassert_mem_equal(result, request, sizeof(result));
    zassert_ok(dp_params_reload());
    zassert_ok(modbus_read_holding_regs(north_client, 16, 31500, result, 3));
    zassert_mem_equal(result, request, sizeof(result));
    zassert_equal(param_status().flags, DP_PARAM_STORAGE_READY);
}

ZTEST(points, test_parameters_downstream_rejection_does_not_change_cache)
{
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_REJECT);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 777),
                  MODBUS_EXC_SERVER_DEVICE_FAILURE);
    zassert_equal(read_word(31500), 100);
    zassert_false(param_status().flags & DP_PARAM_PENDING);
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31500), 100);
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_OK);
}

ZTEST(points, test_parameters_lost_ack_resolved_by_readback)
{
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_LOSE_ACK);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31500, 456));
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count + 1); /* No write retry. */
    zassert_equal(read_word(31500), 456);
    zassert_false(param_status().flags & DP_PARAM_PENDING);
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_OK);
}

ZTEST(points, test_parameters_pending_survives_remount_and_blocks_writes)
{
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_DISCONNECT);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 678),
                  MODBUS_EXC_GW_TARGET_FAILED_TO_RESP);
    zassert_true(param_status().flags & DP_PARAM_PENDING);
    uint16_t value;
    zassert_equal(modbus_read_holding_regs(north_client, 16, 31500, &value, 1),
                  MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31000, 700),
                  MODBUS_EXC_SERVER_DEVICE_BUSY);
    zassert_ok(dp_params_reload());
    zassert_true(param_status().flags & DP_PARAM_PENDING);
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    co_simulator_set_online(&idu_collect_simulator, true);
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_OK);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31093, 1));
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count); /* Recovery only reads. */
    zassert_equal(read_word(31500), 678);
    zassert_false(param_status().flags & DP_PARAM_PENDING);
}

ZTEST(points, test_parameters_ambiguous_readback_never_replays)
{
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_DISCONNECT);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 678),
                  MODBUS_EXC_GW_TARGET_FAILED_TO_RESP);
    co_simulator_set_online(&idu_collect_simulator, true);
    co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_OK);
    zassert_ok(co_simulator_set_register(&idu_collect_simulator, 3, 40000, 333));
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    zassert_equal(dp_params_recover(), -EBADMSG);
    zassert_true(param_status().flags & DP_PARAM_PENDING);
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count);
    /* Device inspected/reset by an operator to its captured pre-write value. */
    zassert_ok(co_simulator_set_register(&idu_collect_simulator, 3, 40000, 100));
    zassert_ok(dp_params_recover());
    zassert_equal(read_word(31500), 100);
    zassert_false(param_status().flags & DP_PARAM_PENDING);
}

ZTEST(points, test_parameters_no_downstream_write_without_durable_intent)
{
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    bytes_before_failure = 0;
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 987),
                  MODBUS_EXC_SERVER_DEVICE_FAILURE);
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count);
    zassert_false(param_status().flags & DP_PARAM_STORAGE_READY);
    zassert_equal(read_word(31500), 100);
    bytes_before_failure = -1;
    zassert_ok(dp_params_reload());
    zassert_ok(dp_params_recover());
}

ZTEST(points, test_parameters_power_loss_after_downstream_before_commit)
{
    writes_at_start = co_simulator_write_count(&idu_collect_simulator);
    fail_after_downstream = true;
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 987),
                  MODBUS_EXC_SERVER_DEVICE_FAILURE);
    zassert_true(param_status().flags & DP_PARAM_PENDING);
    fail_after_downstream = false;
    zassert_ok(dp_params_reload());
    zassert_true(param_status().flags & DP_PARAM_PENDING);
    zassert_ok(dp_params_recover());
    zassert_equal(read_word(31500), 987);
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), writes_at_start + 1);
}

ZTEST(points, test_parameters_torn_flash_write_keeps_whole_batch)
{
    uint16_t baseline[9];
    zassert_ok(modbus_read_holding_regs(north_client, 16, 31000, baseline, 9));
    uint16_t request[9] = {700, 0x7465, 0x7374, 0x2d69, 0x6475, 0, 0, 0, 0};
    /* Fail each possible byte in the journal, individual records, CRC, and metadata,
     * plus successful writes. Repeated iterations also exercise NVS GC. */
    for (int cut = 0; cut < 256; ++cut) {
        zassert_ok(dp_params_write_registers(31000, baseline, 9));
        bytes_before_failure = cut;
        int err = dp_params_write_registers(31000, request, 9);
        bytes_before_failure = -1;
        zassert_true(err == 0 || err == -EIO, "cut=%d error=%d", cut, err);
        zassert_ok(dp_params_reload(), "cut=%d", cut);
        uint16_t result[9];
        zassert_ok(modbus_read_holding_regs(north_client, 16, 31000, result, 9));
        zassert_true(memcmp(result, baseline, sizeof(result)) == 0 ||
                     memcmp(result, request, sizeof(result)) == 0,
                     "torn batch at byte %d", cut);
        if (!err) {
            zassert_mem_equal(result, request, sizeof(result));
        }
    }
}

ZTEST(points, test_parameters_restore_flush_and_broadcast)
{
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    zassert_equal(modbus_write_holding_reg(north_client, 16, 31090, 0),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(read_word(31000), 750);
    uint16_t commands[] = {0xa55a, 0xa55a};
    uint32_t sequence = param_status().sequence;
    zassert_equal(modbus_write_holding_regs(north_client, 16, 31090, commands, 2),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(read_word(31000), 750);
    zassert_equal(param_status().sequence, sequence);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31090, 0xa55a));
    zassert_equal(read_word(31000), 500);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31500, 750));
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31091, 0xa55a));
    zassert_equal(read_word(31500), 100);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31092, 1));
    zassert_equal(modbus_write_holding_reg(north_client, 0, 31000, 800), -ETIMEDOUT);
    zassert_equal(read_word(31000), 500);
}

ZTEST(points, test_process_commands_do_not_latch_or_replay)
{
    const struct dp_key key = {IDU_DEV_LOCAL, IDU_SIG_RESTORE_LOCAL};
    struct dp_point_state before, after;
    zassert_ok(dp_read(key, &before));
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    zassert_ok(dp_process_init_device(IDU_DEV_LOCAL));
    zassert_equal(read_word(31000), 750);
    uint16_t ignored;
    zassert_equal(modbus_read_holding_regs(north_client, 16, 31090, &ignored, 1),
                  MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(read_word(31000), 750);
    uint16_t command = 0xa55a;
    zassert_ok(modbus_write_holding_regs(north_client, 16, 31090, &command, 1));
    zassert_equal(read_word(31000), 500);
    zassert_ok(dp_read(key, &after));
    zassert_equal(after.value.words[0], before.value.words[0]);
    zassert_equal(after.generation, before.generation);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31000), 750);
}

ZTEST(points, test_status_point_is_live_and_read_only)
{
    uint16_t words[4];
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
    struct dp_param_status expected = param_status();
    zassert_ok(modbus_read_holding_regs(north_client, 16, 32000, words, 4));
    zassert_equal(words[0], expected.flags);
    zassert_equal(words[1], (uint16_t)(int16_t)expected.error);
    zassert_equal(words[2], expected.sequence & 0xffff);
    zassert_equal(words[3], expected.sequence >> 16);
    zassert_equal(modbus_write_holding_regs(north_client, 16, 32000, words, 4),
                  MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(modbus_write_holding_reg(north_client, 16, 32001, 7),
                  MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(param_status().sequence, expected.sequence);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 600));
    expected = param_status();
    zassert_ok(modbus_read_holding_regs(north_client, 16, 32002, words, 2));
    zassert_equal(words[0], expected.sequence & 0xffff);
    zassert_equal(words[1], expected.sequence >> 16);
}

ZTEST(points, test_parameters_unknown_format_resets_settings)
{
    struct flash_pages_info page;
    zassert_ok(flash_get_page_info_by_offs(flash_area_get_device(storage), storage->fa_off, &page));
    struct nvs_fs other = {.flash_device = flash_area_get_device(storage), .offset = storage->fa_off,
        .sector_size = page.size, .sector_count = storage->fa_size / page.size};
    zassert_ok(nvs_mount(&other));
    uint8_t unknown[12] = {0};
    zassert_equal(nvs_write(&other, 3, unknown, sizeof(unknown)), sizeof(unknown));
    zassert_ok(dp_params_reload());
    zassert_equal(param_status().flags, DP_PARAM_STORAGE_READY | DP_PARAM_STORAGE_RESET);
    zassert_equal(read_word(31000), CONFIG_IDU_SAMPLE_PERIOD_MS);
    zassert_ok(dp_params_flush());
    zassert_ok(dp_params_reload());
    zassert_equal(param_status().flags, DP_PARAM_STORAGE_READY); /* Reset only once. */
}

ZTEST(points, test_parameters_business_api_callbacks_and_permissions)
{
    struct dp_point_update values[2] = {
        {.key = {1, IDU_SIG_SAMPLE_PERIOD}, .width = 1, .value.words = {750}},
        {.key = {1, 31100}, .width = 1, .value.words = {42}},
    };
    zassert_equal(dp_params_set_values(values, 2), -ERANGE);
    zassert_equal(read_word(31000), 500);
    values[1].value.words[0] = 43;
    zassert_ok(dp_params_set_values(values, 2));
    zassert_ok(dp_params_reload());
    struct dp_point_state state;
    zassert_ok(dp_params_read(values[1].key, &state));
    zassert_equal(state.value.words[0], 43);
    values[1].key.sig_id = 31101;
    zassert_equal(dp_params_set_values(values, 2), -EACCES);
    values[1].key = values[0].key;
    zassert_equal(dp_params_set_values(values, 2), -EINVAL);
    values[1].key = (struct dp_key){IDU_DEV_REMOTE_IO, IDU_SIG_REMOTE_SETPOINT};
    unsigned count = co_simulator_write_count(&idu_collect_simulator);
    zassert_equal(dp_params_set_values(values, 2), -EXDEV); /* Mixed local/remote. */
    zassert_equal(co_simulator_write_count(&idu_collect_simulator), count);
    zassert_ok(dp_params_restore(IDU_DEV_LOCAL));
    zassert_ok(dp_params_read((struct dp_key){1, 31100}, &state));
    zassert_equal(state.value.words[0], 10);
    zassert_ok(dp_params_read((struct dp_key){1, 31101}, &state));
    zassert_equal(state.value.words[0], 7);
}

ZTEST(points, test_parameters_corrupt_value_recovers_valid_history)
{
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 601));
    zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 602));
    uint32_t seq = param_status().sequence;
    size_t memory_size;
    uint8_t *memory = flash_simulator_get_memory(flash_area_get_device(storage), &memory_size);
    zassert_true(storage->fa_off + storage->fa_size <= memory_size);
    unsigned corruptions = 0;
    for (size_t off = storage->fa_off; off + 16 <= storage->fa_off + storage->fa_size; ++off) {
        if (sys_get_le32(memory + off) == 0x56554449 &&
            sys_get_le16(memory + off + 6) == IDU_DEV_LOCAL &&
            sys_get_le16(memory + off + 8) == IDU_SIG_SAMPLE_PERIOD &&
            sys_get_le32(memory + off + 12) == seq) {
            memory[off + 12] ^= 0x80; /* Leave NVS metadata intact; data CRC must detect it. */
            ++corruptions;
        }
    }
    zassert_true(corruptions > 0);
    zassert_ok(dp_params_reload());
    zassert_equal(read_word(31000), 601);
    zassert_true(param_status().flags & DP_PARAM_HISTORY_RECOVERED);
}

ZTEST(points, test_parameters_reboot_probe)
{
    const char *phase = getenv("IDU_REBOOT_PHASE");
    if (!phase) {
        ztest_test_skip(); /* run.sh runs this in three separate processes. */
        return;
    }
    if (!strcmp(phase, "write")) {
        zassert_ok(modbus_write_holding_reg(north_client, 16, 31000, 750));
        co_simulator_set_write_fault(&idu_collect_simulator, CO_SIMULATOR_WRITE_DISCONNECT);
        zassert_equal(modbus_write_holding_reg(north_client, 16, 31500, 987),
                      MODBUS_EXC_GW_TARGET_FAILED_TO_RESP);
        zassert_true(param_status().flags & DP_PARAM_PENDING);
    } else if (!strcmp(phase, "recover")) {
        zassert_equal(read_word(31000), 750);
        zassert_true(param_status().flags & DP_PARAM_PENDING);
        /* The virtual slave RAM restarts with the simulator. Model a real
         * independently powered slave that retained the completed command. */
        zassert_ok(co_simulator_set_register(&idu_collect_simulator, 3, 40000, 987));
        unsigned count = co_simulator_write_count(&idu_collect_simulator);
        zassert_ok(dp_params_recover());
        zassert_equal(co_simulator_write_count(&idu_collect_simulator), count);
        zassert_equal(read_word(31500), 987);
    } else {
        zassert_equal(strcmp(phase, "verify"), 0);
        zassert_equal(read_word(31000), 750);
        zassert_equal(read_word(31500), 987);
        zassert_false(param_status().flags & DP_PARAM_PENDING);
    }
}

extern unsigned module_reboot_count(void);
ZTEST(points, test_parameters_upgrade_flush_freezes_transactions)
{
    struct dp_point_update update = {.key = {1,31100}, .width = 1, .value.words = {77}};
    zassert_ok(dp_params_apply(&update, 1, DP_OP_DEFER));
    zassert_ok(dp_params_prepare_reboot());
    update.value.words[0] = 88;
    zassert_equal(dp_params_apply(&update, 1, 0), -EBUSY);
    zassert_equal(dp_params_restore(IDU_DEV_LOCAL), -EBUSY);
    zassert_equal(dp_params_reload(), -EBUSY);
    dp_params_cancel_reboot();
    zassert_ok(dp_params_reload());
    struct dp_point_state state;
    zassert_ok(dp_params_read(update.key, &state));
    zassert_equal(state.value.words[0], 77);
    zassert_ok(dp_params_apply(&update, 1, 0));
}

ZTEST(points,test_parameters_failed_save_cannot_reboot)
{
    struct dp_point_update update={.key={1,31100},.width=1,.value.words={61}};
    zassert_ok(dp_params_apply(&update,1,DP_OP_DEFER));
    unsigned count=module_reboot_count();
    bytes_before_failure=0;
    zassert_ok(dp_process_save_reboot(100)); k_sleep(K_MSEC(150));
    zassert_equal(module_reboot_count(),count);
    zassert_false(param_status().flags & DP_PARAM_STORAGE_READY);
    bytes_before_failure=-1;
    zassert_ok(dp_params_reload());
}
