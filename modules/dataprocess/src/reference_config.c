#include <platform_callback.h>
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <dataprocess/point_parameters.h>
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
static dp_model_period_fn model_period;
static dp_country_parameter_fn country_override;
static uint16_t parameter_country;
static bool frozen, initializing;
static bool registry_started, registry_starting;

static const uint8_t zero_defaults[144];

static const DM_SIG_CFG_S *configured_signal(uint16_t dev, uint16_t sig)
{
    if (!dev || dev > UINT8_MAX) { return NULL; }
    const DEV_TABLE_S *devices = get_devices ? get_devices() : NULL;
    size_t count = get_device_count ? get_device_count() : 0;
    for (size_t i = 0; devices && i < count; ++i) {
        if (devices[i].dev_id != dev) { continue; }
        const DEV_TBL_CFG_S *group = &devices[i].ptl_tbl_cfg[0];
        if (group->proto_type != PRO_DATA_PROCESS || !group->pst_table) { return NULL; }
        const DM_SIG_CFG_S *rows = group->pst_table;
        for (size_t n = 0; n < group->sig_num; ++n) {
            if (rows[n].sig_id == sig) { return &rows[n]; }
        }
        return NULL;
    }
    return NULL;
}

static int parameter_values(uint16_t country, uint16_t dev, const DM_SIG_CFG_S *row,
                             DM_SIG_DATA_S *value, bool *restore)
{
    if (!row->sig_size || row->sig_size > sizeof(zero_defaults)) { return -EINVAL; }
    DM_SIG_DATA_S data = row->data;
    bool enabled = !(row->sig_flag & FLAG_NO_RESTORE);
    if (country != DP_COUNTRY_COMMON) {
        if (!country_override) { return -ENOTSUP; }
        int err = country_override(country, dev, row->sig_id, &data, &enabled);
        if (err == -ENOENT) {
            data = row->data; enabled = !(row->sig_flag & FLAG_NO_RESTORE);
        } else if (err) { return err < 0 ? err : -EINVAL; }
    }
    if (row->sig_size > 4 && !data.def_value.pst) {
        data.def_value.pst = (void *)zero_defaults;
    }
    *value = data;
    *restore = enabled;
    return 0;
}

int dp_point_parameters_get(uint16_t country, uint16_t dev, uint16_t sig,
                             DM_SIG_DATA_S *value, bool *restore)
{
    if (!value || !restore) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    const DM_SIG_CFG_S *row = configured_signal(dev, sig);
    return row ? parameter_values(country, dev, row, value, restore) : -ENOENT;
}

int dp_register_parameter_country(uint16_t country, dp_country_parameter_fn override)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (country != DP_COUNTRY_COMMON && !override) { return -EINVAL; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = frozen || initializing || dp_config() ? -EBUSY : get_range ? -EINVAL : 0;
    if (!err) { parameter_country = country; country_override = override; }
    k_mutex_unlock(&configuration_lock);
    return err;
}

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
    if (!value || !recover_default) { return -1; }
    if (get_range) { return get_range(dev_id, sig_id, value, recover_default); }
    bool restore;
    int err = dp_point_parameters_get(parameter_country, dev_id, sig_id, value, &restore);
    if (!err) { *recover_default = false; }
    return err;
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

int dp_register_model_period(dp_model_period_fn convert)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = frozen || initializing || dp_config() ? -EBUSY : 0;
    if (!err) { model_period = convert; }
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
    if (dp_stopped()) { return; }
    struct reference_timer *timer = CONTAINER_OF(k_work_delayable_from_work(work),
                                                 struct reference_timer, work);
    int err = dp_reference_submit(timer->function, true);
    /* 不得在系统工作队列执行应用回调。业务队列满时保留此定时器，进行有界重试，
     * 避免忙等。
     */
    k_work_reschedule(&timer->work, K_MSEC(err ? MIN(timer->period_ms, 10U) :
                                               timer->period_ms));
}
#endif

int dp_reference_init(void)
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
            found |= dp_config_key(cfg, p).dev_id == init[i].dev_id;
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
/* 仅传输标识和配置对象需要可写存储；
 * 三张源表及默认值均保留在应用持有的 Flash 中。
 */
static struct dp_device_cfg devices[CONFIG_DP_MAX_DEVICES];
static struct dp_equip_cfg compiled;

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

static int point_values(uint8_t dev, const DM_SIG_CFG_S *signal, struct dp_point_cfg *p)
{
    if (!signal->sig_size || signal->sig_size > DP_POINT_WORDS * 2 ||
        signal->sig_size > 144 || !signal->sig_id) { return -EINVAL; }
    /* 同时作为实时遥测的参考参数需要明确的产品模型；
     * 导入时不得悄悄改变持久化规则。
     */
    if (signal->sig_flag & ~(FLAG_PERSISTENCE | FLAG_COLLECT | FLAG_RT_STORE |
                            FLAG_READ_CALLBACK | FLAG_PERSISTENCE_SYNC |
                            FLAG_NO_RESTORE | FLAG_RANGE_CHECK)) {
        return -ENOTSUP;
    }
    if ((signal->sig_flag & (FLAG_RT_STORE | FLAG_PERSISTENCE_SYNC)) &&
        !(signal->sig_flag & FLAG_PERSISTENCE)) { return -EINVAL; }
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
    if ((signal->sig_flag & FLAG_RANGE_CHECK) && p->type == DP_WORDS) { return -ENOTSUP; }
    DM_SIG_DATA_S defaults;
    bool restore;
    int err = parameter_values(parameter_country, dev, signal, &defaults, &restore);
    if (err) { return err; }
    /* 旧消费者可以继续提供完整默认/范围回调；应用可以直接使用行内配置。 */
    bool recover_default = false;
    if (get_range) {
        if (country_override || parameter_country != DP_COUNTRY_COMMON) { return -EINVAL; }
        defaults = (DM_SIG_DATA_S){0};
        if (get_range(dev, signal->sig_id, &defaults, &recover_default)) { return -EINVAL; }
    }
    /* 强制以默认值覆盖现有 NVS 记录属于产品迁移决策，
     * 不能将此回调标志普遍理解为可安全覆盖。
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
    /* 新表显式启用范围；旧回调保留非零上下限即检查的约定。 */
    if ((signal->sig_flag & FLAG_RANGE_CHECK) ||
        (get_range && (defaults.min_value.u32 || defaults.max_value.u32))) {
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
    if (signal->sig_flag & (FLAG_PERSISTENCE | FLAG_COLLECT)) {
        if (!IS_ENABLED(CONFIG_DP_PARAMS)) { return -ENOTSUP; }
        p->source = DP_SOURCE_PARAMETER;
        p->param_flags = DP_PARAM_WRITE;
        if (signal->sig_flag & FLAG_PERSISTENCE) {
            p->param_flags |= DP_PARAM_PERSIST;
            if (restore) { p->param_flags |= DP_PARAM_RESTORE; }
        }
        if ((signal->sig_flag & FLAG_PERSISTENCE) &&
            !(signal->sig_flag & (FLAG_RT_STORE | FLAG_PERSISTENCE_SYNC))) {
            p->param_flags |= DP_PARAM_DEFER_STORE;
        }
    }
    return 0;
}


static const DM_SIG_CFG_S *signal_row(const DEV_TABLE_S *dev, uint16_t sig)
{
    const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[0];
    const DM_SIG_CFG_S *rows = group->pst_table;
    for (size_t i = 0; i < group->sig_num; ++i) {
        if (rows[i].sig_id == sig) { return &rows[i]; }
    }
    return NULL;
}

static int reference_point(const struct dp_equip_cfg *cfg, size_t index,
                            struct dp_point_cfg *p)
{
    const DEV_TABLE_S *dev = NULL;
    for (size_t d = 0; d < cfg->reference_device_count; ++d) {
        dev = &cfg->reference_devices[d];
        if (index < dev->ptl_tbl_cfg[0].sig_num) { break; }
        index -= dev->ptl_tbl_cfg[0].sig_num;
        dev = NULL;
    }
    if (!dev) { return -ENOENT; }
    const DM_SIG_CFG_S *signal = &((const DM_SIG_CFG_S *)dev->ptl_tbl_cfg[0].pst_table)[index];
    int err = point_values(dev->dev_id, signal, p);
    if (err) { return err; }
    bool acquired = false, forwarded = false;
    for (size_t t = 1; t < MAX_PTL_TBL_NUM; ++t) {
        const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[t];
        if (group->proto_type == PRO_MB_MASTER) {
            const MODBUS_RTU_TABLE_S *rows = group->pst_table;
            for (size_t i = 0; i < group->sig_num; ++i) {
                const MODBUS_RTU_TABLE_S *row = &rows[i];
                if (row->sig_id != signal->sig_id) { continue; }
                if (signal->sig_flag & FLAG_COLLECT) {
                    if (forwarded || row->reg_type != 3 || row->reg_num != p->width ||
                        (row->flag & ~(7U | CO_TABLE_INIT)) || row->data_type || row->scan_pri ||
                        !(row->flag & (p->width == 1 ? 2 : 4))) {
                        return -ENOTSUP;
                    }
                    p->param_flags |= DP_PARAM_FORWARD;
                    p->write_address = row->reg_addr;
                    err = word_order(row->byte_order, &p->write_order);
                    if (err) { return err; }
                    forwarded = true;
                    acquired |= row->period_ms != 0 || (row->flag & CO_TABLE_INIT);
                } else { acquired = true; }
            }
        } else if (group->proto_type == PRO_UPP_MASTER) {
            const UPP_M_MOD_REG *models = group->pst_table;
            for (size_t m = 0; m < models->model_num; ++m) {
                const UPP_M_MOD *model = &models->p_model_ary[m];
                for (size_t i = 0; i < model->data_num; ++i) {
                    acquired |= model->p_data[i].sig_id == signal->sig_id;
                }
            }
        }
    }
    if ((signal->sig_flag & FLAG_COLLECT) && !forwarded) { return -ENOTSUP; }
    if (acquired) {
        if (p->source == DP_SOURCE_PARAMETER && !forwarded) { return -ENOTSUP; }
        if (!forwarded) { p->source = DP_SOURCE_MODBUS; }
    }
    if (p->source == DP_SOURCE_PARAMETER &&
        !pltcb_para_get_is_load_def(p->key.dev_id, p->key.sig_id, signal->sig_size,
                                    (uint8_t *)p->initial.words)) {
        p->param_flags &= ~DP_PARAM_RESTORE;
    }
    return 0;
}

static int reference_collect(const struct dp_equip_cfg *cfg, size_t index,
                              struct co_cfg *poll)
{
    for (size_t d = 0; d < cfg->reference_device_count; ++d) {
        const DEV_TABLE_S *dev = &cfg->reference_devices[d];
        for (size_t t = 1; t < MAX_PTL_TBL_NUM; ++t) {
            const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[t];
            if (group->proto_type == PRO_MB_MASTER) {
                const MODBUS_RTU_TABLE_S *rows = group->pst_table;
                for (size_t i = 0; i < group->sig_num; ++i) {
                    const MODBUS_RTU_TABLE_S *row = &rows[i];
                    const DM_SIG_CFG_S *sig = signal_row(dev, row->sig_id);
                    if (!row->period_ms && !(row->flag & CO_TABLE_INIT)) { continue; }
                    if (index) { --index; continue; }
                    if (!sig || row->reg_num != (sig->sig_size + 1U) / 2U ||
                        (row->reg_type != 3 && row->reg_type != 4)) { return -EINVAL; }
                    /* 每行都是精确请求，因此已满足 FRAME 约束。 */
                    if ((row->flag & ~(((sig->sig_flag & FLAG_COLLECT) ? 7U : 1U) | CO_TABLE_INIT)) ||
                        row->data_type || row->scan_pri) { return -ENOTSUP; }
                    *poll = (struct co_cfg){.dev_id = dev->dev_id,
                        .function = row->reg_type, .address = row->reg_addr,
                        .count = row->reg_num, .priority = row->priority,
                        .field_count = 1};
                    poll->period_ms = row->period_ms;
                    poll->init_only = (row->flag & CO_TABLE_INIT) != 0;
                    poll->reference_field = (struct co_field){.sig_id = row->sig_id,
                        .width = row->reg_num};
                    poll->fields = &poll->reference_field;
                    return word_order(row->byte_order, &poll->reference_field.order);
                }
            } else if (group->proto_type == PRO_UPP_MASTER) {
                const UPP_M_MOD_REG *models = group->pst_table;
                for (size_t m = 0; m < models->model_num; ++m) {
                    const UPP_M_MOD *model = &models->p_model_ary[m];
                    if (index >= model->data_num) { index -= model->data_num; continue; }
                    const UPP_M_MOD_DATA *row = &model->p_data[index];
                    const DM_SIG_CFG_S *sig = row->sig_id <= UINT16_MAX ?
                        signal_row(dev, row->sig_id) : NULL;
                    if (!sig || row->data_len != sig->sig_size || !row->mod_data_len ||
                        (uint32_t)row->mod_data_offset + row->mod_data_len > 65536U) {
                        return -EINVAL;
                    }
                    /* 模型后端提供已解码的字数据；此处不执行比例、类型转换，
                     * 也不封装 UPP 通信报文。
                     */
                    if ((row->rw_mode != 1 && row->rw_mode != 3) ||
                        row->byte_order != 0 || row->set_pri ||
                        row->scan_pri || row->dec_p != 1 ||
                        row->mod_data_type != row->data_type ||
                        row->data_type != sig->data_type ||
                        row->mod_data_len != row->data_len || !model_period) {
                        return -ENOTSUP;
                    }
                    *poll = (struct co_cfg){.dev_id = dev->dev_id,
                        .model_protocol = true, .model_id = model->mod_id,
                        .address = row->mod_data_offset, .count = (row->data_len + 1) / 2,
                        .field_count = 1};
                    int err = model_period(row->rqst_cyl, &poll->period_ms);
                    if (err) { return err; }
                    poll->init_only = poll->period_ms == 0;
                    poll->reference_field = (struct co_field){.sig_id = row->sig_id,
                        .width = poll->count, .order = DP_LOW_WORD_FIRST};
                    poll->fields = &poll->reference_field;
                    return 0;
                }
            }
        }
    }
    return -ENOENT;
}

int dp_transmit_from_reference(const MB_TABLE_CELL_T *row, struct dp_transmit_cfg *map)
{
    if (!row || !map) { return -EINVAL; }
    *map = (struct dp_transmit_cfg){.function = row->reg_type,
        .address = row->reg_addr, .count = row->reg_num,
        .key = {row->dev_id, row->sig_id}, .kind = DP_MAP_VALUE};
    return word_order(row->byte_order, &map->order);
}

static int reference_transmit(const struct dp_equip_cfg *cfg, size_t index,
                               struct dp_transmit_cfg *map)
{
    for (size_t d = 0; d < cfg->reference_device_count; ++d) {
        for (size_t t = 1; t < MAX_PTL_TBL_NUM; ++t) {
            const DEV_TBL_CFG_S *group = &cfg->reference_devices[d].ptl_tbl_cfg[t];
            if (group->proto_type != PRO_MB_SLAVE) { continue; }
            if (index >= group->sig_num) { index -= group->sig_num; continue; }
            const MB_TABLE_CELL_T *row = &((const MB_TABLE_CELL_T *)group->pst_table)[index];
            return dp_transmit_from_reference(row, map);
        }
    }
    return -ENOENT;
}

static int compile_tables(void)
{
    const DEV_TABLE_S *table = pltcb_cfg_get_dev_tbl();
    size_t count = pltcb_cfg_get_dev_num();
    if (!table || !count || count > CONFIG_DP_MAX_DEVICES + 2) { return -EINVAL; }
    compiled = (struct dp_equip_cfg){.devices = devices,
        .reference_devices = table, .reference_device_count = count,
        .point_get = reference_point, .collect_get = reference_collect,
        .transmit_get = reference_transmit,
        /* 对旧版重叠表保留精确且完整的测点读取规则。 */
        .exact_overlapping_reads = true};
    /* 跨表查找前先校验所有目录指针。 */
    for (size_t d = 0; d < count; ++d) {
        const DEV_TABLE_S *dev = &table[d];
        if (!dev->dev_id || dev->ptl_tbl_cfg[0].proto_type != PRO_DATA_PROCESS) { return -EINVAL; }
        for (size_t j = 0; j < d; ++j) {
            if (table[j].dev_id == dev->dev_id) { return -EEXIST; }
        }
        for (size_t t = 0; t < MAX_PTL_TBL_NUM; ++t) {
            const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[t];
            if (t && !group->proto_type && !group->sig_num && !group->pst_table) { continue; }
            if (!group->sig_num || !group->pst_table) { return -EINVAL; }
            if (!t) { compiled.point_count += group->sig_num; continue; }
            if (group->proto_type == PRO_MB_SLAVE) { compiled.transmit_count += group->sig_num; }
            else if (group->proto_type == PRO_MB_MASTER) { /* 在下方计数。 */ }
            /* 协议段由 TR 校验/执行，不生成 DP 测点或普通转发行。 */
            else if (group->proto_type == PRO_ADDR_SLAVE) { continue; }
            else if (group->proto_type == PRO_UPP_MASTER) {
                const UPP_M_MOD_REG *models = group->pst_table;
                if (!models->p_model_ary || !models->model_num || models->cfg_mode != 0 ||
                    models->model_num > CONFIG_DP_POINTS_MAX_POLLS) { return -EINVAL; }
                size_t signals = 0;
                for (size_t m = 0; m < models->model_num; ++m) {
                    const UPP_M_MOD *model = &models->p_model_ary[m];
                    if (!model->p_data || !model->data_num) { return -EINVAL; }
                    for (size_t n = 0; n < m; ++n) {
                        if (models->p_model_ary[n].mod_id == model->mod_id) { return -EEXIST; }
                    }
                    signals += model->data_num;
                }
                if (signals != group->sig_num) { return -EINVAL; }
                compiled.collect_count += signals;
            } else { return -ENOTSUP; }
        }
    }
    for (size_t d = 0; d < count; ++d) {
        const DEV_TABLE_S *dev = &table[d];
        bool has_collect = false;
        for (size_t t = 1; t < MAX_PTL_TBL_NUM; ++t) {
            const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[t];
            if (group->proto_type == PRO_UPP_MASTER) { has_collect = true; }
            if (group->proto_type != PRO_MB_MASTER) { continue; }
            has_collect = true;
            const MODBUS_RTU_TABLE_S *rows = group->pst_table;
            for (size_t i = 0; i < group->sig_num; ++i) {
                const DM_SIG_CFG_S *sig = signal_row(dev, rows[i].sig_id);
                if (!sig) { return -ENOENT; }
                if (rows[i].period_ms || (rows[i].flag & CO_TABLE_INIT)) { ++compiled.collect_count; }
            }
        }
        if (has_collect) {
            if (compiled.device_count == ARRAY_SIZE(devices)) { return -ENOSPC; }
            devices[compiled.device_count++] = (struct dp_device_cfg){.dev_id = dev->dev_id,
                .unit_id = dev->coll_addr, .bus_id = dev->port, .dev_code = dev->dev_code};
        }
    }
    return dp_validate(&compiled);
}

#endif

int dp_reference_prepare(const struct dp_equip_cfg **cfg)
{
    if (!cfg || k_is_in_isr()) { return -EINVAL; }
    *cfg = NULL;
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = -ENOTSUP;
#if defined(CONFIG_DP_REFERENCE_TABLES)
    if (native_provider || initializing) { err = -EBUSY; }
    else if (frozen || dp_config()) {
        err = dp_config() == &compiled ? 0 : -EBUSY;
    } else { err = compile_tables(); }
    if (!err) { *cfg = &compiled; }
#endif
    k_mutex_unlock(&configuration_lock);
    return err;
}

int dp_init(void)
{
    if (k_is_in_isr()) { return -1; }
    k_mutex_lock(&configuration_lock, K_FOREVER);
    int err = -EINVAL;
    if (frozen || initializing || dp_config()) { err = -EALREADY; goto out; }
    initializing = true;
    if (native_provider) {
        if (get_devices || get_device_count || get_range || model_period ||
            country_override || parameter_country != DP_COUNTRY_COMMON) {
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
