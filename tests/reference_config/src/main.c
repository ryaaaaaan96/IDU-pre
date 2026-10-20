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
     .rqst_cyl = 200, .priority = 3, .byte_order = BYTE_ORDER_BIG},
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
static unsigned init_calls;
static bool reject_init = true;
K_SEM_DEFINE(timer_fired, 0, 1);

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
        /* The original checker uses min=max=0 for an unrestricted value. */
    } else { return -1; }
    return 0;
}

static int period(uint8_t encoded, uint32_t *milliseconds)
{
    /* Product policy in this fixture, not a claimed legacy encoding. */
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
    zassert_equal(dp_init(), -1); /* Never silently choose one configuration. */
    zassert_ok(dp_register_config_provider(NULL));
    zassert_equal(dp_init(), -1); /* No guessed rqst_cyl conversion. */
    zassert_is_null(dp_config());
    zassert_ok(dp_register_collect_period(period));

    table[0].ptl_tbl_cfg[1].proto_type = PRO_UPP_SLAVE;
    zassert_equal(dp_init(), -1);
    zassert_is_null(dp_config());
    table[0].ptl_tbl_cfg[1].proto_type = PRO_MB_SLAVE;
    local_signals[0].sig_size = 2;
    zassert_equal(dp_init(), -1); /* U8 is one byte even in a word store. */
    zassert_is_null(dp_config());
    local_signals[0].sig_size = 1;
    local_signals[0].sig_flag = FLAG_COLLECT;
    zassert_equal(dp_init(), -1); /* No forwarding without durability policy. */
    local_signals[0].sig_flag = 0;
    acquisition[0].flag = 1;
    zassert_equal(dp_init(), -1); /* Unsupported segmented tables fail closed. */
    acquisition[0].flag = 0;
    acquisition[0].byte_order = 7;
    zassert_equal(dp_init(), -1);
    acquisition[0].byte_order = BYTE_ORDER_BIG;

    zassert_ok(dp_init());
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
    zassert_equal(cfg->collect[0].address, 500);
    zassert_equal(cfg->collect[0].period_ms, 8000);
    zassert_equal(cfg->collect[0].fields[0].order, DP_HIGH_WORD_FIRST);
    zassert_equal(cfg->transmit_count, 1);
    zassert_equal(cfg->transmit[0].order, DP_HIGH_WORD_BYTE_SWAP);
    zassert_equal(cfg->points[2].source, DP_SOURCE_MODBUS);
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

    zassert_equal(dp_reference_start(), -ENOTSUP);
    zassert_equal(init_calls, 0); /* Validate every timer before INIT effects. */
    timers[0].type = 0;
    zassert_equal(dp_reference_start(), -EIO);
    zassert_equal(init_calls, 1);
    zassert_not_equal(k_sem_take(&timer_fired, K_MSEC(40)), 0);
    zassert_equal(dp_get_sig_value(247, 1, 1, value), 1);
    zassert_equal(value[0], 31); /* Earlier successful INIT is not rolled back. */
    zassert_ok(dp_set_init(247));
    zassert_equal(init_calls, 2); /* Per-device INIT includes the registered table. */
    reject_init = false;
    zassert_ok(dp_reference_start());
    zassert_equal(init_calls, 3);
    zassert_ok(k_sem_take(&timer_fired, K_MSEC(200)));
    zassert_equal(dp_reference_start(), -EALREADY);

    zassert_equal(dp_init(), -1);
    zassert_equal(dp_register_config_provider(no_native_config), -EBUSY);
    pltcb_reg_cfg_get_dev_tbl(empty_table);
    zassert_equal(pltcb_cfg_get_dev_tbl(), table);
}

ZTEST_SUITE(reference_configuration, NULL, NULL, NULL, NULL, NULL);
