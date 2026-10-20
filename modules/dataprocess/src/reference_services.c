#include <dataprocess/reference_internal.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp_services, LOG_LEVEL_INF);

size_t dp_reference_size(const struct dp_point_cfg *p)
{
    return p->size_bytes ? p->size_bytes : p->type == DP_U8 ? 1 : p->width * 2;
}

void dp_reference_pack(uint8_t *bytes, const struct dp_value *value, size_t size)
{
    for (size_t i = 0; i < size; ++i) { bytes[i] = value->words[i / 2] >> (8 * (i % 2)); }
}

void dp_reference_unpack(struct dp_value *value, const uint8_t *bytes, size_t size)
{
    memset(value, 0, sizeof(*value));
    for (size_t i = 0; i < size; ++i) { value->words[i / 2] |= (uint16_t)bytes[i] << (8 * (i % 2)); }
}

/* Original catalogs are retained by pointer in flash. Only native configurations
 * without a catalog need bounded identity descriptors (not copied point tables).
 */
static DEV_TABLE_S native_devices[CONFIG_DP_MAX_DEVICES + 2];
static size_t native_count;
static bool catalog_ready;
K_MUTEX_DEFINE(catalog_lock);

static const DEV_TABLE_S *catalog(size_t *count)
{
    *count = 0;
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg || k_is_in_isr()) { return NULL; }
    if (cfg->reference_devices) {
        *count = cfg->reference_device_count;
        return cfg->reference_devices;
    }
    k_mutex_lock(&catalog_lock, K_FOREVER);
    if (!catalog_ready) {
        native_count = 0;
        for (size_t i = 0; i < cfg->point_count + cfg->device_count; ++i) {
            uint16_t dev = i < cfg->point_count ? dp_config_key(cfg, i).dev_id :
                           cfg->devices[i - cfg->point_count].dev_id;
            bool found = false;
            for (size_t n = 0; n < native_count; ++n) { found |= native_devices[n].dev_id == dev; }
            if (found) { continue; }
            if (dev > UINT8_MAX || native_count == ARRAY_SIZE(native_devices)) {
                native_count = 0; k_mutex_unlock(&catalog_lock); return NULL;
            }
            DEV_TABLE_S *d = &native_devices[native_count++];
            *d = (DEV_TABLE_S){.dev_id = dev};
            for (size_t n = 0; n < cfg->device_count; ++n) {
                const struct dp_device_cfg *p = &cfg->devices[n];
                if (p->dev_id == dev) {
                    d->port = p->bus_id; d->coll_addr = p->unit_id; d->dev_code = p->dev_code;
                    break;
                }
            }
        }
        catalog_ready = true;
    }
    *count = native_count;
    k_mutex_unlock(&catalog_lock);
    return native_devices;
}

int32_t dp_dev_get_num(void)
{
    size_t count;
    return catalog(&count) ? (int32_t)count : -ENODEV;
}

const DEV_TABLE_S *dp_dev_get_tbl(void) { size_t count; return catalog(&count); }

const DEV_TABLE_S *dp_dev_get_from_devid(uint16_t dev)
{
    size_t count;
    const DEV_TABLE_S *table = catalog(&count);
    for (size_t i = 0; i < count; ++i) { if (table[i].dev_id == dev) { return &table[i]; } }
    return NULL;
}

int32_t dp_dev_get_from_portid(uint16_t port, DEV_TABLE_S *device)
{
    if (!device || port > UINT8_MAX) { return -EINVAL; }
    size_t count;
    const DEV_TABLE_S *table = catalog(&count);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].port == port) { *device = table[i]; return 0; }
    }
    return -ENOENT;
}

int32_t dp_dev_get_coll_addr(uint8_t dev)
{
    const DEV_TABLE_S *d = dp_dev_get_from_devid(dev); return d ? d->coll_addr : -ENOENT;
}
uint8_t dp_dev_get_port(uint8_t dev)
{
    const DEV_TABLE_S *d = dp_dev_get_from_devid(dev); return d ? d->port : 0;
}
uint16_t dp_dev_get_code(uint8_t dev)
{
    const DEV_TABLE_S *d = dp_dev_get_from_devid(dev); return d ? d->dev_code : 0;
}

const DEV_TBL_CFG_S *dp_devid_get_prototbl(uint8_t dev, uint16_t protocol)
{
    const DEV_TABLE_S *d = dp_dev_get_from_devid(dev);
    if (!d || !protocol) { return NULL; }
    for (size_t i = 0; i < MAX_PTL_TBL_NUM; ++i) {
        if (d->ptl_tbl_cfg[i].proto_type == protocol) { return &d->ptl_tbl_cfg[i]; }
    }
    return NULL;
}

const void *dp_sigcfg_get_by_devcfg(const DEV_TBL_CFG_S *tables, uint16_t protocol, uint16_t *count)
{
    if (!count) { return NULL; }
    *count = 0;
    if (!tables || !protocol) { return NULL; }
    for (size_t i = 0; i < MAX_PTL_TBL_NUM; ++i) {
        if (tables[i].proto_type == protocol && tables[i].pst_table) {
            *count = tables[i].sig_num; return tables[i].pst_table;
        }
    }
    return NULL;
}

const void *dp_sigcfg_get_by_devid(uint16_t dev, uint16_t protocol, uint16_t *count)
{
    const DEV_TABLE_S *d = dp_dev_get_from_devid(dev);
    return dp_sigcfg_get_by_devcfg(d ? d->ptl_tbl_cfg : NULL, protocol, count);
}

static PARA_GET_IS_LOAD_DEF load_policy;
static PARA_GET_IS_RANGE_CHECK range_policy;
static PARA_SIZE_MISMATCH_PRO mismatch_policy;
static PARA_WRITE_LOG log_callback;
static PARA_RECOVER_FINISH recovered_callback;
static struct k_spinlock callbacks_lock;

#define REGISTER(name, type, slot) \
    void name(type callback) { \
        if (k_is_in_isr()) { return; } \
        k_spinlock_key_t key = k_spin_lock(&callbacks_lock); \
        if (!dp_config()) { slot = callback; } \
        k_spin_unlock(&callbacks_lock, key); \
    }
REGISTER(pltcb_reg_para_is_load_def_cb, PARA_GET_IS_LOAD_DEF, load_policy)
REGISTER(pltcb_reg_para_is_range_check, PARA_GET_IS_RANGE_CHECK, range_policy)
REGISTER(pltcb_reg_para_size_mismatch_cb, PARA_SIZE_MISMATCH_PRO, mismatch_policy)
REGISTER(pltcb_reg_para_write_log_cb, PARA_WRITE_LOG, log_callback)
REGISTER(pltcb_reg_para_recover_finish_cb, PARA_RECOVER_FINISH, recovered_callback)

bool pltcb_para_get_is_load_def(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value)
{
    return !load_policy || load_policy(dev, sig, size, value);
}
bool pltcb_para_get_is_range_check(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value)
{
    return !range_policy || range_policy(dev, sig, size, value);
}
bool pltcb_para_size_mismatch_pro(DM_SIG_CFG_DATA_S *data)
{
    return data && mismatch_policy && mismatch_policy(data);
}
void pltcb_para_write_log(DP_PARA_LOG_DATA *data) { if (data && log_callback) { log_callback(data); } }
void pltcb_para_recover_fininsh(void) { if (recovered_callback) { recovered_callback(); } }

struct override {
    struct dp_key key;
    struct dp_value initial;
    int64_t minimum, maximum;
};
static struct override overrides[CONFIG_DP_REFERENCE_OVERRIDES];
static struct k_spinlock overrides_lock;

static struct override effective(const struct dp_point_cfg *point)
{
    struct override result = {.key = point->key, .initial = point->initial,
                              .minimum = point->minimum, .maximum = point->maximum};
    k_spinlock_key_t key = k_spin_lock(&overrides_lock);
    for (size_t i = 0; i < ARRAY_SIZE(overrides); ++i) {
        if (overrides[i].key.dev_id == point->key.dev_id && overrides[i].key.sig_id == point->key.sig_id) { result = overrides[i]; break; }
    }
    k_spin_unlock(&overrides_lock, key);
    return result;
}

static int64_t number(const struct dp_point_cfg *p, const struct dp_value *v)
{
    uint32_t u = v->words[0] | ((uint32_t)v->words[1] << 16);
    switch (p->type) {
    case DP_S16: return (int16_t)v->words[0];
    case DP_S32: return (int32_t)u;
    case DP_U32: return u;
    default: return v->words[0];
    }
}

int dp_reference_range(const struct dp_point_cfg *p, const struct dp_value *v)
{
    if (!p || !v) { return -EINVAL; }
    if (p->type == DP_U8 && v->words[0] > UINT8_MAX) { return -ERANGE; }
    size_t size = dp_reference_size(p);
    if ((size & 1) && (v->words[size / 2] & 0xff00)) { return -ERANGE; }
    if (p->type == DP_WORDS) { return 0; }
    uint8_t bytes[DP_POINT_WORDS * 2];
    dp_reference_pack(bytes, v, size);
    if (!pltcb_para_get_is_range_check(p->key.dev_id, p->key.sig_id, size, bytes)) { return 0; }
    struct override limits = effective(p);
    int64_t n = number(p, v);
    return n < limits.minimum || n > limits.maximum ? -ERANGE : 0;
}

int dp_reference_default(const struct dp_point_cfg *p, struct dp_value *value, bool policy)
{
    if (!p || !value) { return -EINVAL; }
    *value = effective(p).initial;
    if (policy && load_policy) {
        uint8_t bytes[DP_POINT_WORDS * 2];
        size_t size = dp_reference_size(p);
        dp_reference_pack(bytes, value, size);
        if (!pltcb_para_get_is_load_def(p->key.dev_id, p->key.sig_id, size, bytes)) {
            dp_reference_unpack(value, bytes, size);
        }
    }
    return dp_reference_range(p, value);
}

int32_t dp_get_para_def_range(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *data)
{
    if (!data || k_is_in_isr()) { return -EINVAL; }
    struct dp_point_cfg p_scratch;
    const struct dp_point_cfg *p = dp_find((struct dp_key){dev, sig}, &p_scratch);
    if (!p) { return -ENOENT; }
    size_t size = dp_reference_size(p);
    void *buffer = size > 4 ? data->def_value.pst : NULL;
    if (size > 4 && !buffer) { return -EINVAL; }
    struct override v = effective(p);
    *data = (DM_SIG_DATA_S){0};
    if (size > 4) { data->def_value.pst = buffer; }
    dp_reference_pack(size > 4 ? buffer : data->def_value.str, &v.initial, size);
    switch (p->type) {
    case DP_S16: data->min_value.s16 = v.minimum; data->max_value.s16 = v.maximum; break;
    case DP_S32: data->min_value.s32 = v.minimum; data->max_value.s32 = v.maximum; break;
    case DP_U32: data->min_value.u32 = v.minimum; data->max_value.u32 = v.maximum; break;
    default: data->min_value.u16 = v.minimum; data->max_value.u16 = v.maximum; break;
    }
    return 0;
}

int32_t dp_set_para_def_range(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *data)
{
    if (!data || k_is_in_isr()) { return -EINVAL; }
    struct dp_point_cfg p_scratch;
    const struct dp_point_cfg *p = dp_find((struct dp_key){dev, sig}, &p_scratch);
    if (!p) { return -ENOENT; }
    size_t size = dp_reference_size(p);
    const uint8_t *bytes = size > 4 ? data->def_value.pst : data->def_value.str;
    if (!bytes) { return -EINVAL; }
    struct override v = {.key = p->key};
    dp_reference_unpack(&v.initial, bytes, size);
    switch (p->type) {
    case DP_U8: case DP_U16:
        v.minimum = data->min_value.u16; v.maximum = data->max_value.u16; break;
    case DP_S16: v.minimum = data->min_value.s16; v.maximum = data->max_value.s16; break;
    case DP_U32: v.minimum = data->min_value.u32; v.maximum = data->max_value.u32; break;
    case DP_S32: v.minimum = data->min_value.s32; v.maximum = data->max_value.s32; break;
    case DP_WORDS: break;
    default: return -ENOTSUP;
    }
    if (!data->min_value.u32 && !data->max_value.u32) {
        v.minimum = p->type == DP_S16 ? INT16_MIN : p->type == DP_S32 ? INT32_MIN : 0;
        v.maximum = p->type == DP_U8 ? UINT8_MAX : p->type == DP_U16 ? UINT16_MAX :
                    p->type == DP_S16 ? INT16_MAX : p->type == DP_S32 ? INT32_MAX : UINT32_MAX;
    }
    if (v.minimum > v.maximum || (p->type == DP_U8 && v.maximum > UINT8_MAX) ||
        (p->type != DP_WORDS && (number(p, &v.initial) < v.minimum || number(p, &v.initial) > v.maximum))) {
        return -ERANGE;
    }
    k_spinlock_key_t key = k_spin_lock(&overrides_lock);
    int slot = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(overrides); ++i) {
        if (overrides[i].key.dev_id == p->key.dev_id && overrides[i].key.sig_id == p->key.sig_id) { slot = i; break; }
        if (!overrides[i].key.dev_id && slot < 0) { slot = i; }
    }
    if (slot >= 0) { overrides[slot] = v; }
    k_spin_unlock(&overrides_lock, key);
    return slot < 0 ? slot : 0;
}

int32_t dp_set_init(uint16_t dev)
{
    if (!dev || k_is_in_isr()) { return -EINVAL; }
    if (!dp_dev_get_from_devid(dev)) { return -ENOENT; }
#if defined(CONFIG_DP_PROCESS)
    const INIT_DATA_SET_S *table = pltcb_cfg_get_init_reg_tbl();
    uint16_t count = pltcb_cfg_get_init_reg_num();
    if (count && !table) { return -EINVAL; }
    for (size_t i = 0; i < count; ++i) {
        if (table[i].dev_id == dev && !table[i].function) { return -EINVAL; }
    }
    for (size_t i = 0; i < count; ++i) {
        if (table[i].dev_id != dev) { continue; }
        int err = dp_reference_invoke(table[i].function, false);
        if (err) { return err; }
    }
    return dp_process_init_device(dev);
#else
    return -ENOTSUP;
#endif
}

int32_t dp_dm_reset_dev_data(uint8_t dev) { return dp_store_reset_device(dev); }

#if defined(CONFIG_DP_PROCESS)
struct change_event {
    struct dp_key key;
    struct dp_value value;
    uint8_t old[4];
    uint16_t port, address;
    uint32_t timestamp;
};
K_MSGQ_DEFINE(change_events, sizeof(struct change_event), CONFIG_DP_REFERENCE_EVENTS, 4);
static DM_PF_ATTR_SIG_CHG_PROC observers[3];

int32_t dp_reg_sig_callback(DM_PF_ATTR_SIG_CHG_PROC callback)
{
    if (!callback || k_is_in_isr()) { return -EINVAL; }
    k_spinlock_key_t lock = k_spin_lock(&callbacks_lock);
    int err = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(observers); ++i) {
        if (observers[i] == callback) { err = 0; break; }
        if (!observers[i]) { observers[i] = callback; err = 0; break; }
    }
    k_spin_unlock(&callbacks_lock, lock);
    return err;
}

void dp_reference_notify(const struct dp_point_cfg *p, const struct dp_value *old,
                         const struct dp_value *value)
{
    size_t size = dp_reference_size(p);
    if (!memcmp(old->words, value->words, p->width * 2)) { return; }
    k_spinlock_key_t lock = k_spin_lock(&callbacks_lock);
    bool interested = observers[0] || (log_callback && size <= 4);
    k_spin_unlock(&callbacks_lock, lock);
    if (!interested) { return; }
    struct change_event event = {.key = p->key, .value = *value};
    dp_reference_pack(event.old, old, MIN(size, sizeof(event.old)));
    const struct dp_process_request *request = dp_process_current_request();
    if (request) {
        event.port = request->port_id; event.address = request->address;
        for (size_t i = 0; i < request->count; ++i) {
            const struct dp_process_item *item = &request->items[i];
            if (item->point.key.dev_id == p->key.dev_id && item->point.key.sig_id == p->key.sig_id &&
                item->reference_address_valid) { event.address = item->reference_address; break; }
        }
    }
    /* No wall clock is required by DP; zero explicitly denotes unavailable UTC. */
    if (k_msgq_put(&change_events, &event, K_NO_WAIT)) {
        LOG_WRN("Change notification queue full for %u:%u", p->key.dev_id, p->key.sig_id);
    }
}

void dp_reference_drain_events(void)
{
    struct change_event event;
    for (size_t n = 0; n < CONFIG_DP_REFERENCE_EVENTS && !k_msgq_get(&change_events, &event, K_NO_WAIT); ++n) {
        struct dp_point_cfg scratch;
        const struct dp_point_cfg *p = dp_find(event.key, &scratch);
        if (!p) { continue; }
        uint16_t type, size;
        if (dp_get_sig_data_type(p->key.dev_id, p->key.sig_id, &type, &size)) { continue; }
        uint8_t bytes[DP_POINT_WORDS * 2];
        dp_reference_pack(bytes, &event.value, size);
        DM_PF_ATTR_SIG_CHG_PROC callbacks[ARRAY_SIZE(observers)];
        k_spinlock_key_t lock = k_spin_lock(&callbacks_lock);
        memcpy(callbacks, observers, sizeof(callbacks));
        k_spin_unlock(&callbacks_lock, lock);
        for (size_t i = 0; i < ARRAY_SIZE(callbacks); ++i) {
            if (callbacks[i]) { callbacks[i](p->key.dev_id, p->key.sig_id, size, type, bytes); }
        }
        if (size <= 4) {
            DP_PARA_LOG_DATA data = {.dev_id = p->key.dev_id, .sig_id = p->key.sig_id,
                .sig_size = size, .port_id = event.port, .reg_addr = event.address,
                .date_time = event.timestamp, .addr_type = FUNC_PARA_REG_TYPE_MB};
            memcpy(data.old_value.str, event.old, size);
            memcpy(data.new_value.str, bytes, size);
            pltcb_para_write_log(&data);
        }
    }
}
#else
int32_t dp_reg_sig_callback(DM_PF_ATTR_SIG_CHG_PROC callback)
{ ARG_UNUSED(callback); return -ENOTSUP; }
void dp_reference_notify(const struct dp_point_cfg *p, const struct dp_value *old, const struct dp_value *value)
{ ARG_UNUSED(p); ARG_UNUSED(old); ARG_UNUSED(value); }
void dp_reference_drain_events(void) {}
struct dp_process_ctx *dp_reference_context(void) { return NULL; }
#endif

int dp_reference_restore(bool factory)
{
#if defined(CONFIG_DP_PARAMS)
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg || k_is_in_isr()) { return -EINVAL; }
    for (size_t i = 0; i < cfg->point_count; ++i) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, i, &p_scratch);
        if (p->source != DP_SOURCE_PARAMETER || !(p->param_flags & DP_PARAM_RESTORE)) { continue; }
        bool seen = false;
        for (size_t j = 0; j < i; ++j) {
            struct dp_point_cfg q_scratch;
            const struct dp_point_cfg *q = dp_config_point(cfg, j, &q_scratch);
            seen |= q->key.dev_id == p->key.dev_id && q->source == DP_SOURCE_PARAMETER &&
                    (q->param_flags & DP_PARAM_RESTORE);
        }
        if (seen) { continue; }
        int err = dp_params_restore(p->key.dev_id);
        if (err) { return err; }
    }
    int err = factory ? dp_params_flush() : 0;
    if (!err) { pltcb_para_recover_fininsh(); }
    return err;
#else
    ARG_UNUSED(factory); return -ENOTSUP;
#endif
}

int dp_reference_finish(struct dp_process_ctx *ctx, int error)
{
    if (error || !ctx->finish_actions) { return error; }
#if defined(CONFIG_DP_PARAMS)
    int err = 0;
    if (ctx->finish_actions & (DP_FINISH_DEFAULTS | DP_FINISH_FACTORY)) {
        err = dp_reference_restore(ctx->finish_actions & DP_FINISH_FACTORY);
    }
    if (!err && (ctx->finish_actions & (DP_FINISH_FLUSH | DP_FINISH_REBOOT))) {
        err = dp_params_flush();
    }
    if (!err && (ctx->finish_actions & DP_FINISH_REBOOT)) { err = dp_process_save_reboot(100); }
    return err;
#else
    return -ENOTSUP;
#endif
}

void dp_flush_para_store(void)
{
    struct dp_process_ctx *ctx = dp_reference_context();
    if (ctx) { ctx->finish_actions |= DP_FINISH_FLUSH; }
    else { LOG_WRN("dp_flush_para_store requires a callback transaction"); }
}
void dp_set_reboot(void)
{
    struct dp_process_ctx *ctx = dp_reference_context();
    if (ctx) { ctx->finish_actions |= DP_FINISH_REBOOT; }
    else { LOG_WRN("dp_set_reboot requires a callback transaction"); }
}
void dp_reboot_system(void)
{
    if (k_is_in_isr()) { return; }
    if (dp_reference_context()) { dp_set_reboot(); return; }
#if defined(CONFIG_DP_PARAMS)
    int err = dp_process_save_reboot(100);
    if (err) { LOG_ERR("Reboot request failed: %d", err); }
#else
    LOG_ERR("Reboot requires parameter storage");
#endif
}
static void restore_request(bool factory)
{
    if (k_is_in_isr()) { return; }
    struct dp_process_ctx *ctx = dp_reference_context();
    if (ctx) { ctx->finish_actions |= factory ? DP_FINISH_FACTORY : DP_FINISH_DEFAULTS; return; }
    int err = dp_reference_restore(factory);
    if (err) { LOG_ERR("Default recovery failed: %d", err); }
}
void dp_load_def_data(void) { restore_request(false); }
void dp_reset_factory(void) { restore_request(true); }
