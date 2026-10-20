#include <platform_callback.h>
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp_configuration, LOG_LEVEL_INF);

K_MUTEX_DEFINE(configuration_lock);
static CFG_GET_DEV_TBL get_devices;
static CFG_GET_DEV_NUM get_device_count;
static PARA_GET_RANGE_DEF get_range;
static CFG_GET_TIMER_REG_TBL get_timers;
static CFG_GET_TIMER_REG_NUM get_timer_count;
static CFG_GET_INIT_REG_TBL get_initializers;
static CFG_GET_INIT_REG_NUM get_initializer_count;
static dp_config_provider_fn native_provider;
static dp_collect_period_fn collect_period;
static bool frozen, initializing;
static bool registry_started, registry_starting;

#define REGISTER_CALLBACK(name, type, slot) \
    void name(type callback) \
    { \
        if (k_is_in_isr()) { return; } \
        k_mutex_lock(&configuration_lock, K_FOREVER); \
        if (!frozen && !initializing && !dp_config()) { slot = callback; } \
        k_mutex_unlock(&configuration_lock); \
    }

REGISTER_CALLBACK(pltcb_reg_cfg_get_dev_tbl, CFG_GET_DEV_TBL, get_devices)
REGISTER_CALLBACK(pltcb_reg_cfg_get_dev_num, CFG_GET_DEV_NUM, get_device_count)
REGISTER_CALLBACK(pltcb_reg_para_range_def_cb, PARA_GET_RANGE_DEF, get_range)
REGISTER_CALLBACK(pltcb_reg_cfg_get_timer_reg_tbl, CFG_GET_TIMER_REG_TBL, get_timers)
REGISTER_CALLBACK(pltcb_reg_cfg_get_timer_reg_num, CFG_GET_TIMER_REG_NUM, get_timer_count)
REGISTER_CALLBACK(pltcb_reg_cfg_get_init_reg_tbl, CFG_GET_INIT_REG_TBL, get_initializers)
REGISTER_CALLBACK(pltcb_reg_cfg_get_init_reg_num, CFG_GET_INIT_REG_NUM, get_initializer_count)

const DEV_TABLE_S *pltcb_cfg_get_dev_tbl(void)
{
    return get_devices ? get_devices() : NULL;
}

uint16_t pltcb_cfg_get_dev_num(void)
{
    return get_device_count ? get_device_count() : 0;
}

const TIME_DATA_SET_S *pltcb_cfg_get_timer_reg_tbl(void)
{
    return get_timers ? get_timers() : NULL;
}

uint16_t pltcb_cfg_get_timer_reg_num(void)
{
    return get_timer_count ? get_timer_count() : 0;
}

const INIT_DATA_SET_S *pltcb_cfg_get_init_reg_tbl(void)
{
    return get_initializers ? get_initializers() : NULL;
}

uint16_t pltcb_cfg_get_init_reg_num(void)
{
    return get_initializer_count ? get_initializer_count() : 0;
}

int pltcb_para_get_range_def(uint16_t dev_id, uint16_t sig_id,
                           DM_SIG_DATA_S *value, bool *recover_default)
{
    if (!get_range || !value || !recover_default) { return -1; }
    return get_range(dev_id, sig_id, value, recover_default);
}

int dp_register_config_provider(dp_config_provider_fn provider)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = frozen || initializing || dp_config() ? -EBUSY : 0;
    if (!err) { native_provider = provider; }
    k_mutex_unlock(&configuration_lock);
    return err;
}

int dp_register_collect_period(dp_collect_period_fn convert)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = frozen || initializing || dp_config() ? -EBUSY : 0;
    if (!err) { collect_period = convert; }
    k_mutex_unlock(&configuration_lock);
    return err;
}

#if defined(CONFIG_DP_PROCESS)
struct reference_timer {
    struct k_work_delayable work;
    DM_PF_SIG_PROC function;
    uint32_t period_ms;
};
static struct reference_timer reference_timers[CONFIG_DP_REFERENCE_MAX_TIMERS];

static void timer_due(struct k_work *work)
{
    struct reference_timer *timer = CONTAINER_OF(k_work_delayable_from_work(work),
                                                 struct reference_timer, work);
    int err = dp_reference_submit(timer->function, true);
    /* Do not execute application callbacks on the system workqueue. A full
     * business queue retains this timer for a bounded retry, without spinning.
     */
    k_work_reschedule(&timer->work, K_MSEC(err ? MIN(timer->period_ms, 10U) :
                                               timer->period_ms));
}
#endif

int dp_reference_start(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = 0;
    if (!dp_config()) { err = -ENODEV; goto out; }
    if (registry_started || registry_starting) { err = -EALREADY; goto out; }
    if (!!get_timers != !!get_timer_count ||
        !!get_initializers != !!get_initializer_count) { err = -EINVAL; goto out; }
    const INIT_DATA_SET_S *init = pltcb_cfg_get_init_reg_tbl();
    const TIME_DATA_SET_S *timer = pltcb_cfg_get_timer_reg_tbl();
    size_t init_count = pltcb_cfg_get_init_reg_num();
    size_t timer_count = pltcb_cfg_get_timer_reg_num();
    if ((init_count && !init) || (timer_count && !timer)) { err = -EINVAL; goto out; }
#if defined(CONFIG_DP_PROCESS)
    if (timer_count > ARRAY_SIZE(reference_timers) ||
        init_count > CONFIG_DP_POINTS_MAX_POINTS) { err = -ENOSPC; goto out; }
    for (size_t i = 0; i < init_count; ++i) {
        if (!init[i].function) { err = -EINVAL; goto out; }
        bool found = init[i].dev_id == 0;
        const struct dp_equip_cfg *cfg = dp_config();
        for (size_t p = 0; p < cfg->point_count; ++p) {
            found |= cfg->points[p].key.dev_id == init[i].dev_id;
        }
        if (!found) { err = -ENOENT; goto out; }
    }
    for (size_t i = 0; i < timer_count; ++i) {
        if (!timer[i].function || !timer[i].timing) { err = -EINVAL; goto out; }
        if (timer[i].type != 0) { err = -ENOTSUP; goto out; }
    }
    registry_starting = true;
    for (size_t i = 0; i < init_count; ++i) {
        err = dp_reference_invoke(init[i].function, false);
        if (err) { registry_starting = false; goto out; }
    }
    for (size_t i = 0; i < timer_count; ++i) {
        reference_timers[i].function = timer[i].function;
        reference_timers[i].period_ms = timer[i].timing;
        k_work_init_delayable(&reference_timers[i].work, timer_due);
    }
    for (size_t i = 0; i < timer_count; ++i) {
        k_work_schedule(&reference_timers[i].work, K_MSEC(timer[i].timing));
    }
    registry_starting = false;
#else
    if (init_count || timer_count) { err = -ENOTSUP; goto out; }
#endif
    registry_started = true;
out:
    k_mutex_unlock(&configuration_lock);
    return err;
}

#if defined(CONFIG_DP_REFERENCE_TABLES)
static struct dp_point_cfg points[CONFIG_DP_REFERENCE_MAX_POINTS];
static struct dp_device_cfg devices[CONFIG_DP_MAX_DEVICES];
static struct co_cfg polls[CONFIG_DP_REFERENCE_MAX_COLLECT];
static struct co_field fields[CONFIG_DP_REFERENCE_MAX_COLLECT];
static struct dp_transmit_cfg maps[CONFIG_DP_REFERENCE_MAX_TRANSMIT];
static struct dp_equip_cfg compiled;

static struct dp_point_cfg *find_point(uint16_t dev, uint16_t sig)
{
    for (size_t i = 0; i < compiled.point_count; ++i) {
        if (points[i].key.dev_id == dev && points[i].key.sig_id == sig) {
            return &points[i];
        }
    }
    return NULL;
}

static int word_order(uint8_t encoded, enum dp_word_order *order)
{
    switch (encoded) {
    case BYTE_ORDER_DEFAULT:
    case BYTE_ORDER_BIGSMALL: *order = DP_LOW_WORD_FIRST; return 0;
    case BYTE_ORDER_BIG: *order = DP_HIGH_WORD_FIRST; return 0;
    case BYTE_ORDER_SMALL: *order = DP_LOW_WORD_BYTE_SWAP; return 0;
    case BYTE_ORDER_SMALLBIG: *order = DP_HIGH_WORD_BYTE_SWAP; return 0;
    default: return -ENOTSUP;
    }
}

static int compile_point(uint8_t dev, const DM_SIG_CFG_S *signal)
{
    if (!signal->sig_size || signal->sig_size > DP_POINT_WORDS * 2 ||
        signal->sig_size > 144 || !signal->sig_id ||
        find_point(dev, signal->sig_id)) { return -EINVAL; }
    if (compiled.point_count == ARRAY_SIZE(points)) { return -ENOSPC; }
    /* A reference parameter that is also live telemetry needs an explicit
     * product model; importing it must not silently change persistence rules.
     */
    if (signal->sig_flag & ~(FLAG_PERSISTENCE | FLAG_RT_STORE |
                            FLAG_READ_CALLBACK | FLAG_PERSISTENCE_SYNC)) {
        return -ENOTSUP;
    }
    if ((signal->sig_flag & (FLAG_RT_STORE | FLAG_PERSISTENCE_SYNC)) &&
        !(signal->sig_flag & FLAG_PERSISTENCE)) { return -EINVAL; }
    struct dp_point_cfg *p = &points[compiled.point_count];
    *p = (struct dp_point_cfg){
        .key = {dev, signal->sig_id}, .source = DP_SOURCE_LOCAL,
        .width = (signal->sig_size + 1) / 2, .size_bytes = signal->sig_size,
        .reference_data_type = signal->data_type,
        .reference_handler = signal->pf_sig_proc,
        .handler = signal->pf_sig_proc ? dp_reference_process : NULL,
        .read_callback = (signal->sig_flag & FLAG_READ_CALLBACK) != 0,
    };
    switch (signal->data_type) {
    case DATA_TYPE_U8:
        if (signal->sig_size != 1) { return -EINVAL; }
        p->type = DP_U8; p->maximum = UINT8_MAX; break;
    case DATA_TYPE_U16:
        if (signal->sig_size != 2) { return -EINVAL; }
        p->type = DP_U16; p->maximum = UINT16_MAX; break;
    case DATA_TYPE_S16:
        if (signal->sig_size != 2) { return -EINVAL; }
        p->type = DP_S16; p->minimum = INT16_MIN; p->maximum = INT16_MAX; break;
    case DATA_TYPE_U32:
        if (signal->sig_size != 4) { return -EINVAL; }
        p->type = DP_U32; p->maximum = UINT32_MAX; break;
    case DATA_TYPE_S32:
        if (signal->sig_size != 4) { return -EINVAL; }
        p->type = DP_S32; p->minimum = INT32_MIN; p->maximum = INT32_MAX; break;
    case DATA_TYPE_STRING:
    case DATA_TYPE_U8ARRAY:
    case DATA_TYPE_U16ARRAY:
        if (signal->data_type == DATA_TYPE_U16ARRAY && signal->sig_size % 2) {
            return -EINVAL;
        }
        p->type = DP_WORDS; break;
    default: return -ENOTSUP;
    }
    if (get_range) {
        DM_SIG_DATA_S defaults = {0};
        bool recover_default = false;
        if (get_range(dev, signal->sig_id, &defaults, &recover_default)) {
            return -EINVAL;
        }
        /* Forcing defaults over an existing NVS record is a product migration
         * decision, not a safe generic interpretation of this callback flag.
         */
        if (recover_default && (signal->sig_flag & FLAG_PERSISTENCE)) {
            return -ENOTSUP;
        }
        const uint8_t *bytes = signal->sig_size > 4 ? defaults.def_value.pst :
                                                     defaults.def_value.str;
        if (!bytes) { return -EINVAL; }
        for (size_t i = 0; i < signal->sig_size; ++i) {
            p->initial.words[i / 2] |= (uint16_t)bytes[i] << (8 * (i % 2));
        }
        /* The reference range checker treats two zero bounds as unrestricted. */
        if (defaults.min_value.u32 || defaults.max_value.u32) {
            switch (p->type) {
            case DP_U8:
            case DP_U16:
                p->minimum = defaults.min_value.u16; p->maximum = defaults.max_value.u16; break;
            case DP_S16:
                p->minimum = defaults.min_value.s16; p->maximum = defaults.max_value.s16; break;
            case DP_U32:
                p->minimum = defaults.min_value.u32; p->maximum = defaults.max_value.u32; break;
            case DP_S32:
                p->minimum = defaults.min_value.s32; p->maximum = defaults.max_value.s32; break;
            default: break;
            }
        }
    }
    if (signal->sig_flag & FLAG_PERSISTENCE) {
        if (!IS_ENABLED(CONFIG_DP_PARAMS)) { return -ENOTSUP; }
        p->source = DP_SOURCE_PARAMETER;
        p->param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE;
        if (!(signal->sig_flag & (FLAG_RT_STORE | FLAG_PERSISTENCE_SYNC))) {
            p->param_flags |= DP_PARAM_DEFER_STORE;
        }
    }
    ++compiled.point_count;
    return 0;
}

static int compile_poll(uint8_t dev, const MODBUS_RTU_TABLE_S *row)
{
    struct dp_point_cfg *p = find_point(dev, row->sig_id);
    if (!p || p->source != DP_SOURCE_LOCAL || row->reg_num != p->width ||
        (row->reg_type != 3 && row->reg_type != 4)) { return -EINVAL; }
    /* Segment/grouping, write commands and scan-priority policy require a
     * dedicated adapter; one supported row becomes one exact Modbus request.
     */
    if (row->flag || row->data_type || row->scan_pri || !collect_period) {
        return -ENOTSUP;
    }
    if (compiled.collect_count == ARRAY_SIZE(polls)) { return -ENOSPC; }
    uint32_t period = 0;
    int err = collect_period(row->rqst_cyl, &period);
    if (err) { return err; }
    size_t n = compiled.collect_count;
    fields[n] = (struct co_field){.sig_id = row->sig_id, .width = p->width};
    err = word_order(row->byte_order, &fields[n].order);
    if (err) { return err; }
    polls[n] = (struct co_cfg){.dev_id = dev, .function = row->reg_type,
        .address = row->reg_addr, .count = row->reg_num, .period_ms = period,
        .fields = &fields[n], .field_count = 1, .priority = row->priority,
        .init_only = period == 0};
    p->source = DP_SOURCE_MODBUS;
    ++compiled.collect_count;
    return 0;
}

static int compile_map(const MB_TABLE_CELL_T *row)
{
    if (compiled.transmit_count == ARRAY_SIZE(maps)) { return -ENOSPC; }
    struct dp_transmit_cfg *m = &maps[compiled.transmit_count];
    *m = (struct dp_transmit_cfg){.function = row->reg_type,
        .address = row->reg_addr, .count = row->reg_num,
        .key = {row->dev_id, row->sig_id}, .kind = DP_MAP_VALUE};
    int err = word_order(row->byte_order, &m->order);
    if (err) { return err; }
    ++compiled.transmit_count;
    return 0;
}

static int compile_tables(void)
{
    const DEV_TABLE_S *table = pltcb_cfg_get_dev_tbl();
    size_t count = pltcb_cfg_get_dev_num();
    if (!table || !count || count > CONFIG_DP_MAX_DEVICES) { return -EINVAL; }
    compiled = (struct dp_equip_cfg){.points = points, .devices = devices,
        .collect = polls, .transmit = maps};
    for (size_t d = 0; d < count; ++d) {
        const DEV_TABLE_S *device = &table[d];
        const DEV_TBL_CFG_S *process = &device->ptl_tbl_cfg[0];
        if (!device->dev_id || process->proto_type != PRO_DATA_PROCESS ||
            !process->sig_num || !process->pst_table) { return -EINVAL; }
        for (size_t j = 0; j < d; ++j) {
            if (table[j].dev_id == device->dev_id) { return -EEXIST; }
        }
        const DM_SIG_CFG_S *signals = process->pst_table;
        for (size_t s = 0; s < process->sig_num; ++s) {
            int err = compile_point(device->dev_id, &signals[s]);
            if (err) { return err; }
        }
    }
    for (size_t d = 0; d < count; ++d) {
        const DEV_TABLE_S *device = &table[d];
        bool has_collect = false;
        for (size_t t = 1; t < MAX_PTL_TBL_NUM; ++t) {
            const DEV_TBL_CFG_S *group = &device->ptl_tbl_cfg[t];
            if (group->proto_type == PRO_DEFAULT && !group->sig_num && !group->pst_table) {
                continue;
            }
            if (!group->sig_num || !group->pst_table) { return -EINVAL; }
            for (size_t s = 0; s < group->sig_num; ++s) {
                int err;
                switch (group->proto_type) {
                case PRO_MB_MASTER:
                    err = compile_poll(device->dev_id,
                        &((const MODBUS_RTU_TABLE_S *)group->pst_table)[s]);
                    has_collect = true; break;
                case PRO_MB_SLAVE:
                    err = compile_map(&((const MB_TABLE_CELL_T *)group->pst_table)[s]); break;
                default: return -ENOTSUP;
                }
                if (err) { return err; }
            }
        }
        /* Native device descriptors represent acquisition endpoints only.
         * Local/virtual device identities remain in the process point keys.
         */
        if (has_collect) {
            devices[compiled.device_count++] = (struct dp_device_cfg){
                .dev_id = device->dev_id, .unit_id = device->coll_addr,
                .bus_id = device->port};
        }
    }
    return dp_validate(&compiled);
}
#endif

int dp_init(void)
{
    if (k_is_in_isr()) { return -1; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = -EINVAL;
    if (frozen || initializing || dp_config()) { err = -EALREADY; goto out; }
    initializing = true;
    if (native_provider) {
        if (get_devices || get_device_count || get_range || collect_period) {
            err = -EINVAL;
        } else {
            err = dp_init_config(native_provider());
        }
    } else {
#if defined(CONFIG_DP_REFERENCE_TABLES)
        err = compile_tables();
        if (!err) { err = dp_init_config(&compiled); }
#else
        err = -ENOTSUP;
#endif
    }
    initializing = false;
    frozen = !err;
out:
    k_mutex_unlock(&configuration_lock);
    if (err) { LOG_ERR("Point configuration failed: %d", err); }
    return err ? -1 : 0;
}
