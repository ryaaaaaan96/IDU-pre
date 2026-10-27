#include "../../fixtures/dp_tasks.h"
#include <zephyr/ztest.h>
#include <platform_callback.h>
#include <dataprocess/points.h>
#include <errno.h>

static DM_SIG_CFG_S local_signals[] = {
    {.sig_id = 1, .sig_size = 1, .data_type = DATA_TYPE_U8},
    {.sig_id = 2, .sig_size = 5, .data_type = DATA_TYPE_U8ARRAY},
};
static DM_SIG_CFG_S remote_signals[] = {
    {.sig_id = 10, .sig_size = 4, .data_type = DATA_TYPE_U32},
};
static MODBUS_RTU_TABLE_S acquisition[] = {
    {.sig_id = 10, .reg_addr = 500, .reg_num = 2, .reg_type = 4,
     .period_ms = 8000, .priority = 3, .byte_order = BYTE_ORDER_BIG},
};
static MB_TABLE_CELL_T forwarding[] = {
    {.sig_id = 10, .dev_id = 11, .reg_addr = 100, .reg_num = 2,
     .reg_type = 3, .byte_order = BYTE_ORDER_SMALLBIG},
};
static DEV_TABLE_S table[] = {
    {.dev_id = 247, .ptl_tbl_cfg = {
        {PRO_DATA_PROCESS, ARRAY_SIZE(local_signals), local_signals},
        {PRO_MB_SLAVE, ARRAY_SIZE(forwarding), forwarding},
    }},
    {.dev_id = 11, .port = 0, .coll_addr = 1, .ptl_tbl_cfg = {
        {PRO_DATA_PROCESS, ARRAY_SIZE(remote_signals), remote_signals},
        {PRO_MB_MASTER, ARRAY_SIZE(acquisition), acquisition},
    }},
};
static const uint8_t identifier[] = {0x01, 0x23, 0x45, 0x67, 0x89};
static unsigned range_calls;
static unsigned view_calls;
static unsigned init_calls;
static bool reject_init = true;
K_SEM_DEFINE(timer_fired, 0, 1);

static int indexed_view(const struct dp_equip_cfg *cfg, size_t index,
                         struct dp_point_cfg *point)
{
    ++view_calls;
    point->key = dp_config_key(cfg, index);
    point->initial.words[0] = index;
    return 0;
}

ZTEST(reference_configuration, test_device_scoped_lookup_preserves_table_order)
{
    const DM_SIG_CFG_S signals[] = {
        {.sig_id = 31}, {.sig_id = 2}, {.sig_id = 17},
    };
    const DEV_TABLE_S devices[] = {
        {.dev_id = 40, .ptl_tbl_cfg = {{PRO_DATA_PROCESS, ARRAY_SIZE(signals), signals}}},
        {.dev_id = 7, .ptl_tbl_cfg = {{PRO_DATA_PROCESS, ARRAY_SIZE(signals), signals}}},
    };
    const struct dp_equip_cfg config = {
        .reference_devices = devices, .reference_device_count = ARRAY_SIZE(devices),
        .point_count = 6, .point_get = indexed_view,
    };
    struct dp_point_cfg scratch;
    view_calls = 0;
    zassert_equal(dp_config_index(&config, (struct dp_key){7, 2}), 4);
    const struct dp_point_cfg *point = dp_config_find(&config, (struct dp_key){7, 2}, &scratch);
    zassert_not_null(point);
    zassert_equal(point->key.dev_id, 7);
    zassert_equal(point->initial.words[0], 4);
    zassert_equal(view_calls, 1); /* 不计算其他设备的默认值。 */
    zassert_equal(dp_config_index(&config, (struct dp_key){40, 2}), 1);
    zassert_equal(dp_config_index(&config, (struct dp_key){40, 99}), -ENOENT);
    zassert_is_null(dp_config_find(&config, (struct dp_key){99, 2}, &scratch));
    zassert_equal(view_calls, 1);
}

static int32_t initialize(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out, void *new, void *old)
{
    zassert_is_null(in); zassert_is_null(out);
    zassert_is_null(new); zassert_is_null(old);
    ++init_calls;
    uint8_t value = 31;
    FUNC_PARA_S output = {.dev_id = 247, .sig_id = 1, .sig_size = 1, .pvalue = &value};
    return dp_set_para_value(&output);
}

static int32_t initialize_second(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                                 void *new, void *old)
{
    ARG_UNUSED(in); ARG_UNUSED(out); ARG_UNUSED(new); ARG_UNUSED(old);
    return reject_init ? -EIO : 0;
}

static int32_t timer_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new, void *old)
{
    zassert_is_null(in); zassert_is_null(out);
    zassert_is_null(new); zassert_is_null(old);
    zassert_not_equal(k_current_get(), k_work_queue_thread_get(&k_sys_work_q));
    k_sem_give(&timer_fired);
    return 0;
}

static const INIT_DATA_SET_S initializers[] = {
    {.dev_id = 247, .function = initialize},
    {.dev_id = 11, .function = initialize_second},
};
static TIME_DATA_SET_S timers[] = {
    {.type = 1, .timing = 20, .function = timer_callback},
};
static const INIT_DATA_SET_S *get_init(void) { return initializers; }
static uint16_t init_count(void) { return ARRAY_SIZE(initializers); }
static const TIME_DATA_SET_S *get_timer(void) { return timers; }
static uint16_t timer_count(void) { return ARRAY_SIZE(timers); }

static const DEV_TABLE_S *get_table(void) { return table; }
static const DEV_TABLE_S *empty_table(void) { return NULL; }
static uint16_t device_count(void) { return ARRAY_SIZE(table); }
static const struct dp_equip_cfg *no_native_config(void) { return NULL; }

static int defaults(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *value, bool *recover)
{
    ++range_calls;
    *value = (DM_SIG_DATA_S){0};
    *recover = false;
    if (dev == 247 && sig == 1) {
        value->def_value.u16 = 19;
        value->max_value.u16 = 255;
    } else if (dev == 247 && sig == 2) {
        value->def_value.pst = (void *)identifier;
    } else if (dev == 11 && sig == 10) {
        /* 原始检查器使用 min=max=0 表示数值不受限制。 */
    } else { return -1; }
    return 0;
}

static int period(uint8_t encoded, uint32_t *milliseconds)
{
    /* 这是测试夹具的产品策略，不宣称属于旧版编码。 */
    if (encoded != 200) { return -EINVAL; }
    *milliseconds = 8000;
    return 0;
}

ZTEST(reference_configuration, test_original_tables_validate_before_initialization)
{
    zassert_is_null(pltcb_cfg_get_dev_tbl());
    zassert_equal(pltcb_cfg_get_dev_num(), 0);
    zassert_equal(dp_init(), -1);
    pltcb_reg_cfg_get_dev_tbl(get_table);
    pltcb_reg_cfg_get_dev_num(device_count);
    pltcb_reg_para_range_def_cb(defaults);
    pltcb_reg_cfg_get_init_reg_tbl(get_init);
    pltcb_reg_cfg_get_init_reg_num(init_count);
    pltcb_reg_cfg_get_timer_reg_tbl(get_timer);
    pltcb_reg_cfg_get_timer_reg_num(timer_count);

    zassert_ok(dp_register_config_provider(no_native_config));
    zassert_equal(dp_init(), -1); /* 不得静默选择其中一份配置。 */
    zassert_ok(dp_register_config_provider(NULL));
    zassert_is_null(dp_config());

    table[0].ptl_tbl_cfg[1].proto_type = PRO_UPP_SLAVE;
    zassert_equal(dp_init(), -1);
    zassert_is_null(dp_config());
    table[0].ptl_tbl_cfg[1].proto_type = PRO_MB_SLAVE;
    local_signals[0].sig_size = 2;
    zassert_equal(dp_init(), -1); /* 即使存储按字组织，U8 也只有一个字节。 */
    zassert_is_null(dp_config());
    local_signals[0].sig_size = 1;
    local_signals[0].sig_flag = FLAG_COLLECT;
    zassert_equal(dp_init(), -1); /* 没有南向目标，也未启用参数引擎。 */
    local_signals[0].sig_flag = 0;
    acquisition[0].flag = 8;
    zassert_equal(dp_init(), -1); /* 不支持 FC17 策略时直接拒绝。 */
    acquisition[0].flag = 0;
    acquisition[0].byte_order = 7;
    zassert_equal(dp_init(), -1);
    acquisition[0].byte_order = BYTE_ORDER_BIG;

    /* 不登记应用换算函数；各 Modbus 表行携带实际间隔，
     * 包括 60 秒和 16 位最大值。 */
    const struct dp_equip_cfg *prepared;
    acquisition[0].period_ms = 0;
    zassert_ok(dp_reference_prepare(&prepared));
    zassert_equal(prepared->collect_count, 0, "Zero alone must not select initialization");
    const uint16_t intervals[] = {0, 1, 500, 8000, 60000, UINT16_MAX};
    for (size_t i = 0; i < ARRAY_SIZE(intervals); ++i) {
        acquisition[0].period_ms = intervals[i];
        acquisition[0].flag = intervals[i] ? 0 : CO_TABLE_INIT;
        zassert_ok(dp_reference_prepare(&prepared));
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(prepared, 0, &scratch);
        zassert_not_null(poll);
        zassert_equal(poll->period_ms, intervals[i]);
        zassert_equal(poll->init_only, intervals[i] == 0);
    }
    acquisition[0].period_ms = 8000;
    acquisition[0].flag = 0;

    /* 模型表使用登记表指针及字段总数。 */
    UPP_M_MOD_DATA model_field = {.sig_id = 10, .mod_data_offset = 20,
        .mod_data_len = 4, .mod_data_type = DATA_TYPE_U32,
        .data_len = 4, .data_type = DATA_TYPE_U32, .rw_mode = 1,
        .rqst_cyl = 200, .dec_p = 1};
    UPP_M_MOD model = {.p_data = &model_field, .data_num = 1, .mod_id = 99};
    UPP_M_MOD_REG registry = {.p_model_ary = &model, .model_num = 1};
    DEV_TBL_CFG_S original = table[1].ptl_tbl_cfg[1];
    table[1].ptl_tbl_cfg[1] = (DEV_TBL_CFG_S){PRO_UPP_MASTER, 1, &registry};
    zassert_not_equal(dp_reference_prepare(&prepared), 0); /* 缺少周期策略。 */
    zassert_is_null(prepared);
    zassert_ok(dp_register_model_period(period));
    model_field.dec_p = 2;
    zassert_not_equal(dp_reference_prepare(&prepared), 0);
    model_field.dec_p = 1;
    model_field.data_type = DATA_TYPE_U16;
    zassert_not_equal(dp_reference_prepare(&prepared), 0);
    model_field.data_type = DATA_TYPE_U32;
    table[1].ptl_tbl_cfg[1].sig_num = 2;
    zassert_not_equal(dp_reference_prepare(&prepared), 0);
    table[1].ptl_tbl_cfg[1].sig_num = 1;
    zassert_ok(dp_reference_prepare(&prepared));
    struct co_cfg model_poll;
    zassert_not_null(dp_config_collect(prepared, 0, &model_poll));
    zassert_true(model_poll.model_protocol);
    zassert_equal(model_poll.model_id, 99);
    zassert_equal(model_poll.address, 20);
    zassert_equal(model_poll.count, 2);
    table[1].ptl_tbl_cfg[1] = original;

    zassert_ok(dp_init()); fixture_start_dp();
    const struct dp_equip_cfg *cfg = dp_config();
    zassert_not_null(cfg);
    zassert_equal(dp_dev_get_num(), ARRAY_SIZE(table));
    zassert_equal_ptr(dp_dev_get_tbl(), table);
    zassert_equal_ptr(dp_dev_get_from_devid(11), &table[1]);
    zassert_equal_ptr(dp_devid_get_prototbl(11, PRO_MB_MASTER), &table[1].ptl_tbl_cfg[1]);
    uint16_t signal_count;
    zassert_equal_ptr(dp_sigcfg_get_by_devid(247, PRO_DATA_PROCESS, &signal_count), local_signals);
    zassert_equal(signal_count, ARRAY_SIZE(local_signals));
    zassert_equal(cfg->point_count, 3);
    zassert_equal(cfg->device_count, 1);
    zassert_equal(cfg->devices[0].dev_id, 11);
    zassert_equal(cfg->devices[0].unit_id, 1);
    zassert_equal(cfg->collect_count, 1);
    struct co_cfg poll;
    zassert_not_null(dp_config_collect(cfg, 0, &poll));
    zassert_is_null(cfg->collect); /* 不在 RAM 中复制整张表。 */
    zassert_equal(poll.address, 500);
    zassert_equal(poll.period_ms, 8000);
    zassert_equal(poll.fields[0].order, DP_HIGH_WORD_FIRST);
    zassert_equal(cfg->transmit_count, 1);
    struct dp_transmit_cfg map;
    zassert_not_null(dp_config_transmit(cfg, 0, &map));
    zassert_is_null(cfg->transmit);
    zassert_equal(map.order, DP_HIGH_WORD_BYTE_SWAP);
    struct dp_point_cfg point;
    zassert_not_null(dp_config_point(cfg, 2, &point));
    zassert_is_null(cfg->points);
    zassert_equal(point.source, DP_SOURCE_MODBUS);
    zassert_true(range_calls >= 3);
    uint8_t value[7] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
    zassert_equal(dp_get_sig_value(247, 1, 1, value + 1), 1);
    zassert_equal(value[0], 0xaa);
    zassert_equal(value[1], 19);
    zassert_equal(value[2], 0xaa);
    zassert_equal(dp_get_sig_value(247, 2, 5, value + 1), 5);
    zassert_mem_equal(value + 1, identifier, sizeof(identifier));
    zassert_equal(value[6], 0xaa);
    uint8_t remote[] = {0xfe, 0xdc, 0xba, 0x98};
    zassert_ok(dp_set_sig_value(11, 10, sizeof(remote), remote));
    zassert_equal(dp_get_sig_value(11, 10, sizeof(value), value), sizeof(value));
    zassert_mem_equal(value, remote, sizeof(remote));

    /* 覆盖项使用信号键，不使用临时栈视图的地址。 */
    DM_SIG_DATA_S limits = {.def_value.u16 = 42, .max_value.u16 = 100};
    zassert_ok(dp_set_para_def_range(247, 1, &limits));
    limits = (DM_SIG_DATA_S){.def_value.u32 = 123, .max_value.u32 = UINT32_MAX};
    zassert_ok(dp_set_para_def_range(11, 10, &limits));
    zassert_ok(dp_get_para_def_range(247, 1, &limits));
    zassert_equal(limits.def_value.u16, 42);
    zassert_equal(limits.max_value.u16, 100);
    zassert_ok(dp_get_para_def_range(11, 10, &limits));
    zassert_equal(limits.def_value.u32, 123);
    zassert_equal(local_signals[0].sig_id, 1);

    zassert_equal(dp_reference_init(), -ENOTSUP);
    zassert_equal(init_calls, 0); /* 产生 INIT 副作用前先校验所有定时器。 */
    timers[0].type = 0;
    zassert_equal(dp_reference_init(), -EIO);
    zassert_equal(init_calls, 1);
    zassert_not_equal(k_sem_take(&timer_fired, K_MSEC(40)), 0);
    zassert_equal(dp_get_sig_value(247, 1, 1, value), 1);
    zassert_equal(value[0], 31); /* 此前成功的 INIT 不回滚。 */
    zassert_ok(dp_set_init(247));
    zassert_equal(init_calls, 2); /* 按设备执行 INIT 时也包含已登记表。 */
    reject_init = false;
    zassert_ok(dp_reference_init());
    zassert_equal(init_calls, 3);
    zassert_ok(k_sem_take(&timer_fired, K_MSEC(200)));
    zassert_equal(dp_reference_init(), -EALREADY);

    zassert_equal(dp_init(), -1);
    zassert_equal(dp_register_config_provider(no_native_config), -EBUSY);
    pltcb_reg_cfg_get_dev_tbl(empty_table);
    zassert_equal(pltcb_cfg_get_dev_tbl(), table);
}

ZTEST_SUITE(reference_configuration, NULL, NULL, NULL, NULL, NULL);
