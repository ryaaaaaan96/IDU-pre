#include "../../fixtures/dp_tasks.h"
#include <data_process.h>
#include <dataprocess/acquisition.h>
#include <platform_callback.h>
#include <dataprocess/params.h>
#include <dataprocess/reference_internal.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <string.h>
#if defined(CONFIG_DP_PARAMS)
#include <zephyr/kvss/nvs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#endif

static atomic_t changed, logged, recovered, rebooted, migrated;
static bool bypass_range, custom_default, reject_handler, reject_migration;
static int handler_mode, init_calls, transport_mode, completion_calls;
static uint16_t old_registered;
static PARA_DATA_S *pending;
static PARA_SET_STATUS pending_done;
static void *retained_allocation;
static bool retained_released;
void __real_k_free(void *ptr);
void __wrap_k_free(void *ptr)
{
    if (ptr && ptr == retained_allocation) { retained_released = true; }
    __real_k_free(ptr);
}
static DP_PARA_LOG_DATA last_log;
K_SEM_DEFINE(event_received, 0, 16);
K_SEM_DEFINE(log_received, 0, 16);

static int handler(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out, void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    if (in->type == DP_PARA_INIT) { ++init_calls; return 0; }
    if (handler_mode == 1) {
        DATA_SET_S item = {.dev_id = 1, .sig_id = 1, .sig_size = 2, .p_value.u16 = 77};
        zassert_ok(dp_para_value_reg(PARA_VALUE_REG_OLD, &item));
        uint8_t bytes[2];
        FUNC_PARA_S query = {.dev_id = 1, .sig_id = 1, .sig_size = 2, .pvalue = bytes};
        zassert_ok(dp_get_para_old_value(&query)); old_registered = sys_get_le16(bytes);
        item.p_value.u16 = 66;
        zassert_ok(dp_para_value_reg(PARA_VALUE_REG_NEW, &item));
    } else if (handler_mode == 2) {
        dp_flush_para_store(); dp_set_reboot();
    } else if (handler_mode == 3) {
        DATA_SET_S item = {.dev_id = 1, .sig_id = 1, .sig_size = 2, .p_value.u16 = 44};
        PARA_DATA_S request = {.set_num = 1, .p_set_data = &item};
        zassert_ok(dp_set_para_not_dm(&request, NULL));
    }
    return reject_handler ? -ECANCELED : 0;
}

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {10}, .handler = dp_reference_process,
     .reference_handler = handler, .reference_data_type = DATA_TYPE_U16},
    {.key = {1, 2}, .source = DP_SOURCE_LOCAL, .type = DP_WORDS, .width = 3,
     .initial.words = {0x0201, 0x0403, 5}, .size_bytes = 5},
    {.key = {2, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1,
     .maximum = UINT16_MAX, .initial.words = {7}},
    {.key = {1, 3}, .source = DP_SOURCE_LOCAL, .type = DP_U8, .width = 1, .maximum = 100},
#if defined(CONFIG_DP_PARAMS)
    {.key = {2, 9}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1,
     .maximum = 100, .initial.words = {25}, .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_FORWARD,
     .write_address = 100},
    {.key = {1, 5}, .source = DP_SOURCE_PARAMETER, .type = DP_WORDS, .width = 3,
     .initial.words = {1, 2, 3}, .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE},
    {.key = {1, 6}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1, .maximum = 100,
     .initial.words = {20}, .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE},
#endif
};
static const struct dp_device_cfg devices[] = {{.dev_id = 2, .unit_id = 9, .bus_id = 1, .dev_code = 42}};
#if defined(CONFIG_DP_PARAMS)
static const struct co_field feedback_field[] = {{.sig_id = 9, .width = 1}};
static const struct co_cfg feedback[] = {{.dev_id = 2, .function = 3, .address = 100,
    .count = 1, .period_ms = 100, .fields = feedback_field, .field_count = 1}};
static uint16_t remote_setting = 25;
static int read_setting(void *user, uint16_t dev, uint16_t address, uint16_t *words, size_t count)
{
    ARG_UNUSED(user); zassert_equal(dev, 2); zassert_equal(address, 100); zassert_equal(count, 1);
    words[0] = remote_setting; return 0;
}
static int write_setting(void *user, uint16_t dev, uint16_t address, const uint16_t *words, size_t count)
{
    ARG_UNUSED(user); zassert_equal(dev, 2); zassert_equal(address, 100); zassert_equal(count, 1);
    remote_setting = words[0]; return 0;
}
#endif
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points), .devices = devices,
    .device_count = ARRAY_SIZE(devices),
#if defined(CONFIG_DP_PARAMS)
    .collect = feedback, .collect_count = ARRAY_SIZE(feedback),
#endif
};
static bool range(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{ ARG_UNUSED(dev); ARG_UNUSED(sig); ARG_UNUSED(size); ARG_UNUSED(bytes); return !bypass_range; }
static bool defaults(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{
    ARG_UNUSED(dev);
    if (custom_default && sig == 6 && size == 2) { sys_put_le16(31, bytes); return false; }
    return true;
}
static bool mismatch(DM_SIG_CFG_DATA_S *data)
{
    if (reject_migration) { return false; }
    zassert_equal(data->dev_id, 1); zassert_equal(data->sig_id, 5);
    zassert_equal(data->sig_store_size, 4); zassert_equal(data->sig_cfg_size, 6);
    zassert_equal(data->cfg_data_type, DATA_TYPE_U16ARRAY);
    memcpy(data->p_load_value, data->p_store_value, 4);
    sys_put_le16(0x3333, data->p_load_value + 4);
    atomic_inc(&migrated); return true;
}
static void log_value(DP_PARA_LOG_DATA *data)
{ last_log = *data; atomic_inc(&logged); k_sem_give(&log_received); }
static void recovery(void) { atomic_inc(&recovered); }
static void change(uint16_t dev, uint16_t sig, uint8_t size, uint32_t type, const uint8_t *value)
{
    ARG_UNUSED(dev); ARG_UNUSED(sig); ARG_UNUSED(size); ARG_UNUSED(type); ARG_UNUSED(value);
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){1, 1}, &state)); /* 未持有存储锁或事务锁。 */
    atomic_inc(&changed); k_sem_give(&event_received);
}
static int collect(PARA_DATA_S *data, PARA_SET_STATUS done)
{
    if (transport_mode == 1) { pending = data; pending_done = done; return 0; }
    if (transport_mode == 2) { return -EIO; }
    if (transport_mode == 3) { return -ETIMEDOUT; }
    done(data); return 0;
}
static void complete(PARA_DATA_S *data) { ARG_UNUSED(data); ++completion_calls; }
#if defined(CONFIG_DP_PARAMS)
void dp_process_reboot(void) { atomic_inc(&rebooted); }
static void mount_test_nvs(struct nvs_fs *fs)
{
    const struct flash_area *area;
    struct flash_pages_info page;
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), &area));
    const struct device *dev = flash_area_get_device(area);
    zassert_ok(flash_get_page_info_by_offs(dev, area->fa_off, &page));
    *fs = (struct nvs_fs){.flash_device = dev, .offset = area->fa_off,
        .sector_size = page.size, .sector_count = area->fa_size / page.size};
    zassert_ok(nvs_mount(fs)); flash_area_close(area);
}
/* 仅植入独立的 v2 记录；宽度转换是按键策略，
 * 与表顺序、路由或全局模式指纹无关。 */
static void seed_previous_values(void)
{
    struct nvs_fs fs; mount_test_nvs(&fs);
    uint8_t meta[12] = {0};
    sys_put_le32(0x4d554449, meta); sys_put_le16(2, meta + 4); sys_put_le32(9, meta + 8);
    zassert_true(nvs_write(&fs, 3, meta, sizeof(meta)) >= 0);
    for (uint16_t sig = 5; sig <= 6; ++sig) {
        uint8_t key[4], record[20] = {0};
        sys_put_le16(1, key); sys_put_le16(sig, key + 2);
        uint16_t id = 0x100 + crc32_ieee(key, sizeof(key)) % 1024;
        sys_put_le32(0x56554449, record); sys_put_le16(2, record + 4);
        memcpy(record + 6, key, sizeof(key));
        record[10] = sig == 5 ? DP_WORDS : DP_U16;
        record[11] = sig == 5 ? 2 : 1; sys_put_le32(9, record + 12);
        sys_put_le16(sig == 5 ? 0x1111 : 55, record + 16);
        if (sig == 5) { sys_put_le16(0x2222, record + 18); }
        size_t length = 16 + 2 * record[11];
        zassert_true(nvs_write(&fs, id, record, length) >= 0);
    }
}

#endif
static void *setup(void)
{
    pltcb_reg_para_is_range_check(range);
    pltcb_reg_para_is_load_def_cb(defaults);
    pltcb_reg_para_size_mismatch_cb(mismatch);
    pltcb_reg_para_write_log_cb(log_value);
    pltcb_reg_para_recover_finish_cb(recovery);
    zassert_ok(dp_reg_sig_callback(change));
    zassert_ok(dp_para_reg_set_func(collect));
    zassert_ok(dp_init_config(&equipment)); fixture_start_dp();
#if defined(CONFIG_DP_PARAMS)
    static const struct dp_params_transport transport = {.read = read_setting, .write = write_setting};
    zassert_ok(dp_params_set_transport(&transport));
    seed_previous_values();
    zassert_ok(dp_params_init(&equipment, DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition))));
#endif
    return NULL;
}
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    handler_mode = 0; reject_handler = bypass_range = custom_default = false;
    transport_mode = 0; completion_calls = 0;
    k_sleep(K_MSEC(30));
    atomic_clear(&changed); atomic_clear(&logged); atomic_clear(&recovered); atomic_clear(&rebooted);
    k_sem_reset(&event_received); k_sem_reset(&log_received);
}

ZTEST(reference_services, test_a_per_key_width_conversion)
{
#if defined(CONFIG_DP_PARAMS)
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){1, 5}, &state));
    zassert_mem_equal(state.value.words, ((uint16_t[]){0x1111, 0x2222, 0x3333}), 6);
    zassert_equal(atomic_get(&migrated), 1);
    zassert_ok(dp_params_reload());
    zassert_equal(atomic_get(&migrated), 1); /* 转换后的值恰好保存一次。 */
#else
    zassert_false(pltcb_para_size_mismatch_pro(NULL));
#endif
}

ZTEST(reference_services, test_native_catalog_and_query_contract)
{
    zassert_equal(dp_dev_get_num(), 2); /* 包含本地设备 1。 */
    zassert_not_null(dp_dev_get_tbl());
    zassert_equal(dp_dev_get_from_devid(2)->dev_code, 42);
    zassert_equal(dp_dev_get_code(2), 42); zassert_equal(dp_dev_get_port(2), 1);
    zassert_equal(dp_dev_get_coll_addr(2), 9);
    zassert_is_null(dp_dev_get_from_devid(0));
    DEV_TABLE_S copy;
    zassert_ok(dp_dev_get_from_portid(1, &copy)); zassert_equal(copy.dev_id, 2);
    zassert_equal(dp_dev_get_from_portid(900, &copy), -EINVAL);
    uint16_t count = 123;
    zassert_is_null(dp_sigcfg_get_by_devid(2, PRO_DATA_PROCESS, &count));
    zassert_equal(count, 0); /* 不得将原生结构强转为原始表。 */
    zassert_is_null(dp_devid_get_prototbl(2, PRO_MB_MASTER));
    int previous = init_calls;
    zassert_ok(dp_set_init(1)); zassert_equal(init_calls, previous + 1);
    zassert_equal(dp_set_init(99), -ENOENT);
}

ZTEST(reference_services, test_b_rejected_width_conversion_resets_settings)
{
#if defined(CONFIG_DP_PARAMS)
    seed_previous_values();
    reject_migration = true;
    zassert_ok(dp_params_reload());
    reject_migration = false;
    struct dp_param_status status; dp_params_get_status(&status);
    zassert_true(status.flags & DP_PARAM_STORAGE_RESET);
    struct dp_point_state state; zassert_ok(dp_read((struct dp_key){1, 5}, &state));
    zassert_mem_equal(state.value.words, ((uint16_t[]){1, 2, 3}), 6);
    zassert_ok(dp_params_reload());
    dp_params_get_status(&status); zassert_false(status.flags & DP_PARAM_STORAGE_RESET);
#else
    zassert_false(pltcb_para_size_mismatch_pro(NULL));
#endif
}

ZTEST(reference_services, test_defaults_and_ranges_leave_const_table_unchanged)
{
    DM_SIG_DATA_S data = {.def_value.u16 = 12, .min_value.u16 = 10, .max_value.u16 = 40};
    zassert_ok(dp_set_para_def_range(1, 1, &data));
    memset(&data, 0, sizeof(data)); zassert_ok(dp_get_para_def_range(1, 1, &data));
    zassert_equal(data.def_value.u16, 12); zassert_equal(data.max_value.u16, 40);
    zassert_equal(points[0].initial.words[0], 10); zassert_equal(points[0].maximum, 100);
    uint8_t bytes[] = {50, 0};
    zassert_equal(dp_set_sig_value(1, 1, 2, bytes), -ERANGE);
    bypass_range = true; zassert_ok(dp_set_sig_value(1, 1, 2, bytes));
    zassert_equal(dp_check_value(&points[3], &(struct dp_value){.words = {256}}), -ERANGE);
    bypass_range = false;
    data = (DM_SIG_DATA_S){.def_value.u16 = 10, .max_value.u16 = 100};
    zassert_ok(dp_set_para_def_range(1, 1, &data));
    uint8_t input[] = {9, 8, 7, 6, 5}, output[7] = {0xaa,0,0,0,0,0,0xbb};
    data = (DM_SIG_DATA_S){.def_value.pst = input};
    zassert_ok(dp_set_para_def_range(1, 2, &data)); memset(input, 0, sizeof(input));
    data.def_value.pst = output + 1; zassert_ok(dp_get_para_def_range(1, 2, &data));
    zassert_mem_equal(output, ((uint8_t[]){0xaa,9,8,7,6,5,0xbb}), sizeof(output));
    zassert_equal(points[1].initial.words[0], 0x0201);
}

ZTEST(reference_services, test_change_and_log_follow_commit_with_address)
{
    DATA_SET_S item = {.dev_id=1,.sig_id=1,.sig_size=2,.p_value.u16=37,
                      .extra.reg_addr=5008};
    PARA_DATA_S request = {.port_id=7,.set_num=1,.p_set_data=&item};
    zassert_ok(dp_fast_para_set(&request, NULL));
    zassert_ok(k_sem_take(&event_received, K_MSEC(200)));
    zassert_ok(k_sem_take(&log_received, K_MSEC(200)));
    zassert_equal(last_log.port_id, 7); zassert_equal(last_log.reg_addr, 5008);
    zassert_equal(last_log.new_value.u16, 37); zassert_equal(last_log.date_time, 0);
    int count = atomic_get(&changed);
    zassert_ok(dp_fast_para_set(&request, NULL)); k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&changed), count);
    reject_handler = true; item.p_value.u16 = 38;
    zassert_equal(dp_fast_para_set(&request, NULL), -ECANCELED); k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&changed), count);
}

ZTEST(reference_services, test_registered_values_are_scoped_to_transaction)
{
    DATA_SET_S item = {.dev_id=1,.sig_id=1,.sig_size=2,.p_value.u16=20};
    zassert_equal(dp_para_value_reg(PARA_VALUE_REG_NEW, &item), -EPERM);
    handler_mode = 1;
    zassert_ok(dp_set_sig_value(1,1,2,item.p_value.str));
    zassert_equal(old_registered, 77);
    uint8_t bytes[2]; zassert_equal(dp_get_sig_value(1,1,2,bytes),2);
    zassert_equal(sys_get_le16(bytes),66);
}

ZTEST(reference_services, test_collector_timeout_retains_copied_storage)
{
    uint8_t bytes[] = {1,2,3,4,5};
    DATA_SET_S item = {.dev_id=1,.sig_id=2,.sig_size=5,.p_value.pst=bytes};
    PARA_DATA_S request = {.set_num=1,.p_set_data=&item};
    transport_mode = 1;
    zassert_equal(dp_set_para_not_dm(&request, complete), -ETIMEDOUT);
    retained_allocation = pending;
    retained_released = false;
    memset(bytes, 9, sizeof(bytes));
    zassert_mem_equal(pending->p_set_data[0].p_value.pst, ((uint8_t[]){1,2,3,4,5}),5);
    zassert_equal(dp_set_para_not_dm(&request, complete), -EBUSY);
    zassert_equal(dp_para_reg_set_func(collect), -EBUSY);
    zassert_false(retained_released);
    pending_done(pending); transport_mode = 0;
    zassert_true(retained_released, "Late completion must release the retained copy");
    retained_allocation = NULL;
    zassert_ok(dp_set_para_not_dm(&request, complete)); zassert_equal(completion_calls,1);
    transport_mode = 2;
    zassert_equal(dp_set_para_not_dm(&request, complete), -EIO); zassert_equal(completion_calls,1);
    transport_mode = 3;
    zassert_equal(dp_set_para_not_dm(&request, complete), -ETIMEDOUT);
    transport_mode = 0;
    zassert_ok(dp_set_para_not_dm(&request, complete)); zassert_equal(completion_calls,2);
}

ZTEST(reference_services, test_direct_collect_suppresses_matching_dm_output)
{
    uint8_t old[2], value[2] = {22,0};
    zassert_equal(dp_get_sig_value(1,1,2,old),2);
    handler_mode = 3;
    zassert_ok(dp_set_sig_value(1,1,2,value));
    zassert_equal(dp_get_sig_value(1,1,2,value),2); zassert_mem_equal(value,old,2);
}

ZTEST(reference_services, test_reset_only_remote_device)
{
    uint8_t bytes[2] = {88,0}; zassert_ok(dp_set_sig_value(2,1,2,bytes));
    zassert_equal(dp_dm_reset_dev_data(1), -EACCES);
    zassert_ok(dp_dm_reset_dev_data(2));
    struct dp_point_update old = {.key = {2, 1}, .width = 1, .value.words = {99}};
    zassert_equal(dp_commit_sample(&old, 1, 0, 0), -ECANCELED);
    struct dp_point_state state; zassert_ok(dp_read((struct dp_key){2,1},&state));
    zassert_equal(state.value.words[0],7); zassert_equal(state.quality,DP_QUALITY_UNINITIALIZED);
}

ZTEST(reference_services, test_restore_policy_and_finish_notification)
{
#if defined(CONFIG_DP_PARAMS)
    custom_default = true;
    dp_load_def_data(); zassert_equal(atomic_get(&recovered),1);
    struct dp_point_state state; zassert_ok(dp_read((struct dp_key){1,6},&state));
    zassert_equal(state.value.words[0],31);
    custom_default = false;
    dp_reset_factory(); zassert_equal(atomic_get(&recovered),2);
    zassert_ok(dp_params_reload()); zassert_ok(dp_read((struct dp_key){1,6},&state));
    zassert_equal(state.value.words[0],20);
#else
    zassert_equal(dp_reference_restore(false), -ENOTSUP);
#endif
}

ZTEST(reference_services, test_transaction_failure_cancels_reboot)
{
    uint8_t value[] = {30,0}; handler_mode = 2; reject_handler = true;
    zassert_equal(dp_set_sig_value(1,1,2,value), -ECANCELED);
    k_sleep(K_MSEC(200)); zassert_equal(atomic_get(&rebooted),0);
    reject_handler = false;
#if defined(CONFIG_DP_PARAMS)
    zassert_ok(dp_set_sig_value(1,1,2,value));
    k_sleep(K_MSEC(200)); zassert_equal(atomic_get(&rebooted),1);
#else
    zassert_equal(dp_set_sig_value(1,1,2,value), -ENOTSUP);
#endif
}
ZTEST(reference_services, test_z_offline_feedback_keeps_durable_target_and_unrelated_quality)
{
#if defined(CONFIG_DP_PARAMS)
    struct dp_point_update target = {.key = {2, 9}, .width = 1, .value.words = {42}};
    zassert_ok(dp_params_set_values(&target, 1));
    k_msleep(1);
    struct dp_point_update live = {.key = {2, 9}, .width = 1, .value.words = {99}};
    zassert_ok(dp_params_sample(&live, 1, 0, k_uptime_get()));
    k_msleep(1);
    zassert_ok(dp_invalidate_acquired_device(2, -ETIMEDOUT, k_uptime_get()));
    struct dp_point_state state;
    zassert_ok(dp_read(target.key, &state));
    zassert_equal(state.value.words[0], 99);
    zassert_equal(state.quality & 0xff, DP_QUALITY_COMM_ERROR);
    struct dp_point_update local = {.key = {1, 6}, .width = 1, .value.words = {22}};
    zassert_ok(dp_params_set_values(&local, 1));
    zassert_ok(dp_read(target.key, &state));
    zassert_equal(state.quality & 0xff, DP_QUALITY_COMM_ERROR,
                  "An unrelated local parameter commit cannot validate remote feedback");
    zassert_ok(dp_params_sample(&live, 1, 0, 0));
    zassert_ok(dp_read(target.key, &state));
    zassert_equal(state.quality & 0xff, DP_QUALITY_COMM_ERROR);
    zassert_ok(dp_params_reload());
    zassert_ok(dp_params_sync_device(2));
    zassert_equal(remote_setting, 42, "Feedback and offline invalidation never overwrite the saved target");
#else
    ztest_test_skip();
#endif
}

ZTEST_SUITE(reference_services, NULL, setup, before, NULL, NULL);
