#include <dataprocess/params.h>
#include "acquisition_internal.h"
#include "parameter_store.h"
#include <dataprocess/reference_internal.h>
#include <dataprocess/process.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(dp_params, LOG_LEVEL_INF);

#define INTENT_ID 2
#define IMAGE_MAGIC DP_STORE_IMAGE_MAGIC
#define INTENT_MAGIC 0x4a554449U
#define FORMAT_VERSION DP_STORE_VERSION
#define IMAGE_HEADER 16
#define INTENT_HEADER 20
#define IMAGE_MAX CONFIG_DP_PARAMS_TRANSACTION_BYTES
#define INTENT_MAX (INTENT_HEADER + 4 * DP_TRANSFER_WORDS + IMAGE_MAX)

static const struct dp_equip_cfg *equipment;
static uint16_t defs[CONFIG_DP_PARAMS_MAX_COUNT];
/* The compound literal lives in the caller's block; no descriptor is retained. */
#define PARAM_DEF(i) dp_config_point(equipment, defs[i], &(struct dp_point_cfg){0})
static struct dp_point_state cache[CONFIG_DP_PARAMS_MAX_COUNT];
static size_t param_count;
static struct dp_value staged[CONFIG_DP_PARAMS_MAX_COUNT];
/* durable never follows transient NO_STORE cache writes. */
static struct dp_value durable[CONFIG_DP_PARAMS_MAX_COUNT];
static struct dp_value durable_stage[CONFIG_DP_PARAMS_MAX_COUNT];
static struct dp_value deferred[CONFIG_DP_PARAMS_MAX_COUNT];
static bool dirty[CONFIG_DP_PARAMS_MAX_COUNT];
static int64_t flush_at;
static uint8_t operation_flags;
static struct nvs_fs fs;
static const struct flash_area *area;
static uint8_t partition_id;
static struct dp_params_transport transport;
static bool stored[CONFIG_DP_PARAMS_MAX_COUNT];
static struct dp_param_status status;
/* Scratch space is serialized by transaction_lock, never borrowed by readers. */
static uint8_t commit_image[IMAGE_MAX];
static uint8_t intent[INTENT_MAX];
static size_t intent_length;
static uint16_t old_wire[DP_TRANSFER_WORDS], new_wire[DP_TRANSFER_WORDS], actual[DP_TRANSFER_WORDS];
K_MUTEX_DEFINE(transaction_lock);
static bool reboot_frozen;
static bool cache_initialized;
K_MUTEX_DEFINE(cache_lock);
NET_BUF_POOL_FIXED_DEFINE(param_event_pool, 2, sizeof(struct dp_param_status),
                         sizeof(struct zbus_channel *), NULL);
ZBUS_CHAN_DEFINE(dp_param_events, struct dp_param_status, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

int dp_params_set_transport(const struct dp_params_transport *ops)
{
    if (k_is_in_isr() || (ops && !ops->write)) {
        return -EINVAL;
    }
    if (k_mutex_lock(&transaction_lock, K_NO_WAIT)) {
        return -EBUSY;
    }
    const struct dp_params_transport empty = {0};
    if (!ops) {
        ops = &empty;
    }
    bool same = transport.read == ops->read && transport.write == ops->write &&
                transport.simulated == ops->simulated && transport.user == ops->user;
    int err = equipment && !same ? -EBUSY : 0;
    if (!err) {
        transport = *ops;
    }
    k_mutex_unlock(&transaction_lock);
    return err;
}

static int transport_read(uint16_t dev_id, uint16_t address,
                          uint16_t *words, size_t count)
{
    return transport.read ? transport.read(transport.user, dev_id, address, words, count) :
                            -ENODEV;
}

static int transport_write(uint16_t dev_id, uint16_t address,
                           const uint16_t *words, size_t count)
{
    return transport.write ? transport.write(transport.user, dev_id, address, words, count) :
                             -ENODEV;
}

static bool simulated_device(uint16_t dev_id)
{
    return transport.simulated && transport.simulated(transport.user, dev_id);
}

static int range_check(const struct dp_point_cfg *p, const struct dp_value *v)
{
    return dp_reference_range(p, v);
}

int dp_params_validate(const struct dp_equip_cfg *cfg)
{
    size_t count = 0, bytes = IMAGE_HEADER;
    for (size_t i = 0; i < cfg->point_count; ++i) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, i, &p_scratch);
        if (p->source != DP_SOURCE_PARAMETER) {
            if (p->param_flags || p->prepare_write || p->seed_missing) {
                return -EINVAL;
            }
            continue;
        }
        if (++count > CONFIG_DP_PARAMS_MAX_COUNT || p->process || p->stale_ms ||
            (p->param_flags & ~0x1fU) || range_check(p, &p->initial) ||
            ((p->param_flags & DP_PARAM_RESTORE) && !(p->param_flags & DP_PARAM_WRITE)) ||
            (p->seed_missing && !(p->param_flags & DP_PARAM_PERSIST))) {
            LOG_ERR("Invalid parameter %u:%u count=%u flags=%u",p->key.dev_id,p->key.sig_id,(unsigned)count,p->param_flags);
            return -EINVAL;
        }
        if (p->param_flags & DP_PARAM_PERSIST) { bytes += 6 + p->width * 2; }
        if (bytes > IMAGE_MAX) { return -E2BIG; }
        if (p->param_flags & DP_PARAM_FORWARD) {
            bool found = false;
            for (size_t d = 0; d < cfg->device_count; ++d) {
                found |= cfg->devices[d].dev_id == p->key.dev_id;
            }
            if (!found || !(p->param_flags & DP_PARAM_WRITE) ||
                p->write_order > DP_HIGH_WORD_BYTE_SWAP ||
                (uint32_t)p->write_address + p->width > 65536) {
                return -EINVAL;
            }
            for (size_t j = 0; j < i; ++j) {
                struct dp_point_cfg q_scratch;
                const struct dp_point_cfg *q = dp_config_point(cfg, j, &q_scratch);
                if (q->source == DP_SOURCE_PARAMETER &&
                    (q->param_flags & DP_PARAM_FORWARD) && q->key.dev_id == p->key.dev_id &&
                    q->write_address < (uint32_t)p->write_address + p->width &&
                    p->write_address < (uint32_t)q->write_address + q->width) {
                    return -EEXIST;
                }
            }
        }
    }
    return 0;
}

static int index_of(struct dp_key key)
{
    for (size_t i = 0; i < param_count; ++i) {
        struct dp_key candidate = dp_config_key(equipment, defs[i]);
        if (candidate.dev_id == key.dev_id && candidate.sig_id == key.sig_id) {
            return i;
        }
    }
    return -ENOENT;
}

void dp_params_get_status(struct dp_param_status *out)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    *out = status;
    k_mutex_unlock(&cache_lock);
}

static void set_status(int error, uint16_t flags)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    status.error = error;
    status.flags = flags;
    k_mutex_unlock(&cache_lock);
}

static void notify_result(int error)
{
    struct dp_param_status event;
    k_mutex_lock(&cache_lock, K_FOREVER);
    status.error = error;
    event = status;
    k_mutex_unlock(&cache_lock);
    int err = zbus_chan_pub(&dp_param_events, &event, K_NO_WAIT);
    if (err) {
        LOG_WRN("Parameter notification lost: %d", err);
    }
}

int dp_params_read(struct dp_key key, struct dp_point_state *out)
{
    int i = index_of(key);
    if (i < 0 || !out) {
        return i < 0 ? i : -EINVAL;
    }
    k_mutex_lock(&cache_lock, K_FOREVER);
    *out = cache[i];
    k_mutex_unlock(&cache_lock);
    return 0;
}

static void stage_cache(void)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        staged[i] = cache[i].value;
        durable_stage[i] = durable[i];
    }
    k_mutex_unlock(&cache_lock);
}

static void commit_cache(uint32_t sequence, const bool *selected)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        if (selected && !selected[i]) { continue; }
        if ((status.flags & DP_PARAM_STORAGE_READY) ||
            (cache_initialized && !(PARAM_DEF(i)->param_flags & DP_PARAM_PERSIST))) {
            dp_reference_notify(PARAM_DEF(i), &cache[i].value, &staged[i]);
        }
        cache[i].value = staged[i];
        cache[i].quality = ((PARAM_DEF(i)->param_flags & DP_PARAM_FORWARD) && simulated_device(PARAM_DEF(i)->key.dev_id)) ?
                            DP_QUALITY_SIMULATED : DP_QUALITY_GOOD;
        cache[i].sampled_at = k_uptime_get();
        cache[i].generation = sequence;
        cache[i].error = 0;
    }
    status.sequence = sequence;
    k_mutex_unlock(&cache_lock);
}

static int load_value(const struct dp_point_cfg *p, uint8_t type, uint8_t width,
                       const uint8_t *bytes, struct dp_value *value)
{
    if (type != p->type) { return -EPROTO; }
    *value = (struct dp_value){0};
    if (width != p->width) {
        uint8_t converted[DP_POINT_WORDS * 2] = {0};
        static const uint32_t types[] = {
            DATA_TYPE_U16, DATA_TYPE_S16, DATA_TYPE_U32, DATA_TYPE_S32,
            DATA_TYPE_U16ARRAY, DATA_TYPE_U8,
        };
        DM_SIG_CFG_DATA_S data = {.dev_id = p->key.dev_id, .sig_id = p->key.sig_id,
            .sig_store_size = width * 2, .sig_cfg_size = dp_reference_size(p),
            .p_store_value = (void *)bytes, .p_load_value = converted,
            .cfg_data_type = p->reference_data_type ? p->reference_data_type : types[p->type]};
        if (!pltcb_para_size_mismatch_pro(&data)) { return -EPROTO; }
        dp_reference_unpack(value, converted, data.sig_cfg_size);
    } else {
        for (size_t w = 0; w < width; ++w) { value->words[w] = sys_get_le16(bytes + w * 2); }
    }
    return range_check(p, value);
}

/* 临时事务镜像仅包含新建/变化的设定值；不持久化默认值或范围配置。 */
static size_t encode_changes(uint8_t *buf, uint32_t sequence)
{
    size_t offset = IMAGE_HEADER;
    uint16_t count = 0;
    sys_put_le32(IMAGE_MAGIC, buf); sys_put_le16(FORMAT_VERSION, buf + 4);
    sys_put_le32(0, buf + 8); sys_put_le32(sequence, buf + 12);
    for (size_t i = 0; i < param_count; ++i) {
        const struct dp_point_cfg *p = PARAM_DEF(i);
        if (!(p->param_flags & DP_PARAM_PERSIST) ||
            (stored[i] && !memcmp(&durable_stage[i], &durable[i], p->width * 2))) { continue; }
        sys_put_le16(p->key.dev_id, buf + offset); sys_put_le16(p->key.sig_id, buf + offset + 2);
        buf[offset + 4] = p->type; buf[offset + 5] = p->width; offset += 6;
        for (size_t w = 0; w < p->width; ++w) {
            sys_put_le16(durable_stage[i].words[w], buf + offset); offset += 2;
        }
        ++count;
    }
    sys_put_le16(count, buf + 6);
    return offset;
}

static int save_parameters(uint32_t sequence)
{
    size_t len = encode_changes(commit_image, sequence);
    int err = dp_store_commit(&fs, commit_image, len);
    if (!err) {
        memcpy(durable, durable_stage, sizeof(durable));
        for (size_t i = 0; i < param_count; ++i) {
            if (PARAM_DEF(i)->param_flags & DP_PARAM_PERSIST) { stored[i] = true; }
        }
    }
    return err;
}

static uint8_t unit_for(uint16_t dev_id)
{
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (equipment->devices[i].dev_id == dev_id) {
            return equipment->devices[i].unit_id;
        }
    }
    return 0;
}

static uint8_t bus_for_device(uint16_t dev_id)
{
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (equipment->devices[i].dev_id == dev_id) { return equipment->devices[i].bus_id; }
    }
    return UINT8_MAX;
}

static int intent_validate(void)
{
    if (intent_length < INTENT_HEADER || sys_get_le32(intent) != INTENT_MAGIC ||
        sys_get_le16(intent + 4) != FORMAT_VERSION || intent[7] == 0 ||
        (intent[10] & ~DP_OP_MASK) || intent[11] != bus_for_device(sys_get_le16(intent + 18)) || intent[7] > DP_TRANSFER_WORDS || intent[6] != unit_for(sys_get_le16(intent + 18)) ||
        intent[6] == 0 || (uint32_t)sys_get_le16(intent + 8) + intent[7] > 65536 ||
        intent_length != INTENT_HEADER + 4U * intent[7] + sys_get_le16(intent + 16)) {
        return -EPROTO;
    }
    const uint8_t *image = intent + INTENT_HEADER + 4U * intent[7];
    size_t length = sys_get_le16(intent + 16);
    int err = dp_store_validate_image(image, length);
    uint32_t sequence = length >= IMAGE_HEADER ? sys_get_le32(image + 12) : 0;
    for (size_t at = IMAGE_HEADER; !err && at < length; at += 6 + 2U * image[at + 5]) {
        int i = index_of((struct dp_key){sys_get_le16(image + at), sys_get_le16(image + at + 2)});
        if (i < 0) { return -EPROTO; }
        const struct dp_point_cfg *p = PARAM_DEF(i);
        uint16_t begin = sys_get_le16(intent + 8);
        if (!(p->param_flags & DP_PARAM_FORWARD) || !(p->param_flags & DP_PARAM_PERSIST) ||
            p->key.dev_id != sys_get_le16(intent + 18) || p->write_address < begin ||
            (uint32_t)p->write_address + p->width > begin + intent[7]) { return -EPROTO; }
        err = load_value(p, image[at + 4], image[at + 5], image + at + 6, &durable_stage[i]);
        if (!err) {
            for (size_t w = 0; w < p->width; ++w) {
                if (dp_order_word(durable_stage[i].words, p->width, w, p->write_order) !=
                    sys_get_le16(intent + INTENT_HEADER + 2U * intent[7] +
                                 2U * (p->write_address - begin + w))) { return -EPROTO; }
            }
        }
    }
    return err ? err : sequence == sys_get_le32(intent + 12) ? 0 : -EPROTO;
}

static void invalidate_pending(int error)
{
    uint16_t dev_id = sys_get_le16(intent + 18);
    uint16_t address = sys_get_le16(intent + 8);
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        if ((PARAM_DEF(i)->param_flags & DP_PARAM_FORWARD) && PARAM_DEF(i)->key.dev_id == dev_id &&
            PARAM_DEF(i)->write_address >= address &&
            PARAM_DEF(i)->write_address < (uint32_t)address + intent[7]) {
            cache[i].quality = DP_QUALITY_COMM_ERROR |
                               (simulated_device(dev_id) ? DP_QUALITY_SIMULATED : 0);
            cache[i].error = error;
        }
    }
    status.flags |= DP_PARAM_PENDING;
    k_mutex_unlock(&cache_lock);
}

/* Returns 0 if committed, 1 if the device still has the pre-write values.
 * Neither timeout nor an ambiguous result triggers an automatic write retry.
 */
static int recover_locked(void)
{
    if (!(status.flags & DP_PARAM_PENDING)) {
        return 0;
    }
    stage_cache();
    int err = intent_validate();
    if (err) {
        return err;
    }
    size_t count = intent[7];
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    uint16_t device = sys_get_le16(intent + 18), address = sys_get_le16(intent + 8);
    for (size_t i = 0; i < param_count; ++i) {
        const struct dp_point_cfg *p = PARAM_DEF(i);
        selected[i] = (p->param_flags & DP_PARAM_FORWARD) && p->key.dev_id == device &&
            p->write_address >= address && (uint32_t)p->write_address + p->width <= address + count;
    }
    err = transport_read(sys_get_le16(intent + 18), sys_get_le16(intent + 8), actual, count);
    if (err) {
        return err < 0 ? err : -EIO;
    }
    bool is_new = true, is_old = true;
    for (size_t i = 0; i < count; ++i) {
        is_old &= actual[i] == sys_get_le16(intent + INTENT_HEADER + 2 * i);
        is_new &= actual[i] == sys_get_le16(intent + INTENT_HEADER + 2 * count + 2 * i);
    }
    if (!is_new && !is_old) {
        return -EBADMSG;
    }
    uint32_t sequence = sys_get_le32(intent + 12);
    if (is_new) {
        err = save_parameters(sequence);
        if (err) {
            set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
            return err;
        }
        uint8_t flags = sys_get_le16(intent + 10);
        uint16_t dev = sys_get_le16(intent + 18), begin = sys_get_le16(intent + 8);
        for (size_t i = 0; i < param_count; ++i) {
            const struct dp_point_cfg *p = PARAM_DEF(i);
            if (!(p->param_flags & DP_PARAM_FORWARD) || p->key.dev_id != dev ||
                p->write_address < begin || (uint32_t)p->write_address + p->width > begin + count) {
                continue;
            }
            if (!(flags & DP_OP_NO_DM)) {
                for (size_t w = 0; w < p->width; ++w) {
                    staged[i].words[w] = dp_order_word(actual + p->write_address - begin,
                                                        p->width, w, p->write_order);
                }
                if (!(flags & DP_OP_NO_STORE)) {
                    bool defer = (flags & DP_OP_DEFER) || (p->param_flags & DP_PARAM_DEFER_STORE);
                    dirty[i] = defer;
                    if (defer) { deferred[i] = staged[i]; flush_at = k_uptime_get() + 1000; }
                }
            }
        }
        commit_cache(sequence, selected);
    }
    err = nvs_delete(&fs, INTENT_ID);
    if (err < 0) {
        return err;
    }
    if (!is_new) {
        stage_cache(); /* Clear the uncertain quality without applying the candidate. */
        commit_cache(status.sequence, selected);
    }
    intent_length = 0;
    set_status(0, status.flags & ~DP_PARAM_PENDING);
    return is_new ? 0 : 1;
}

static bool incompatible_format(int err)
{
    return err == -EPROTO || err == -EMSGSIZE || err == -EEXIST;
}

static int load_locked(bool *incompatible)
{
    *incompatible = false;
    set_status(0, 0);
    memset(&fs, 0, sizeof(fs));
    struct flash_pages_info page;
    int err = flash_area_open(partition_id, &area);
    if (err) {
        return err;
    }
    fs.flash_device = flash_area_get_device(area);
    if (!device_is_ready(fs.flash_device)) {
        return -ENODEV;
    }
    err = flash_get_page_info_by_offs(fs.flash_device, area->fa_off, &page);
    if (err) {
        return err;
    }
    if (!page.size || page.size > UINT16_MAX || area->fa_off % page.size ||
        area->fa_size % page.size || area->fa_size / page.size < 3 ||
        INTENT_MAX + 64 >= page.size) {
        return -EINVAL;
    }
    fs.offset = area->fa_off;
    fs.sector_size = page.size;
    fs.sector_count = area->fa_size / page.size;
    err = nvs_mount(&fs);
    if (err) {
        return err;
    }
    for (size_t i = 0; i < param_count; ++i) {
        err = dp_reference_default(PARAM_DEF(i), &durable_stage[i], false);
        if (err) { return err; }
        staged[i] = durable_stage[i];
    }
    uint32_t sequence = 0;
    bool recovered = false, present = false, converted = false;
    memset(stored, 0, sizeof(stored));
    err = dp_store_open(&fs, commit_image, sizeof(commit_image), &sequence, &present, &recovered);
    if (err) { *incompatible = incompatible_format(err); return err; }
    bool seed[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    if (present) {
        for (size_t i = 0; i < param_count; ++i) {
            const struct dp_point_cfg *p = PARAM_DEF(i);
            if (!(p->param_flags & DP_PARAM_PERSIST)) { continue; }
            struct dp_stored_value value;
            err = dp_store_read(&fs, p->key, &value, &recovered);
            if (err == -ENOENT) { seed[i] = true; continue; }
            if (err) { *incompatible = incompatible_format(err); return err; }
            uint8_t bytes[DP_STORE_MAX_WORDS * 2];
            for (size_t w = 0; w < value.width; ++w) { sys_put_le16(value.words[w], bytes + w * 2); }
            err = load_value(p, value.type, value.width, bytes, &durable_stage[i]);
            if (err) { *incompatible = incompatible_format(err); return err; }
            stored[i] = value.width == p->width;
            if (!stored[i]) { converted = true; }
        }
    } else {
        /* ID 1 belonged to the retired aggregate format. No payload is imported. */
        uint8_t probe;
        ssize_t legacy = nvs_read(&fs, 1, &probe, sizeof(probe));
        if (legacy != -ENOENT) {
            *incompatible = legacy >= 0;
            return legacy < 0 ? (int)legacy : -EPROTO;
        }
        for (size_t i = 0; i < param_count; ++i) { seed[i] = true; }
    }
    bool need_save = !present;
    for (size_t i = 0; i < param_count; ++i) {
        const struct dp_point_cfg *p = PARAM_DEF(i);
        if (seed[i]) {
            err = p->seed_missing ? p->seed_missing(p, &durable_stage[i]) :
                                   dp_reference_default(p, &durable_stage[i], true);
            if (!err) { err = range_check(p, &durable_stage[i]); }
            if (err) { return err; }
        }
        need_save |= (p->param_flags & DP_PARAM_PERSIST) && !stored[i];
    }
    memcpy(staged, durable_stage, sizeof(staged));
    memcpy(durable, durable_stage, sizeof(durable));
    memset(dirty, 0, sizeof(dirty)); flush_at = 0;
    commit_cache(sequence, NULL);
    ssize_t len = nvs_read(&fs, INTENT_ID, intent, sizeof(intent));
    intent_length = 0;
    if (len != -ENOENT) {
        if (len < 0) { return len; }
        if (len > sizeof(intent)) { *incompatible = true; return -EMSGSIZE; }
        intent_length = len;
        stage_cache();
        err = intent_validate();
        if (err) { *incompatible = incompatible_format(err); return err; }
    }
    /* 只保存已确认的设定值。南向候选值必须等待读回确认。 */
    stage_cache();
    if (need_save) {
        err = save_parameters(sequence);
        if (err) { return err; }
    }
    set_status(0, DP_PARAM_STORAGE_READY |
               (recovered ? DP_PARAM_HISTORY_RECOVERED : 0) |
               (converted ? DP_PARAM_VALUE_CONVERTED : 0) |
               (intent_length ? DP_PARAM_PENDING : 0));
    if (intent_length) {
        invalidate_pending(-EINPROGRESS);
    }
    return 0;
}

/* Format changes explicitly discard settings; I/O failures never trigger erase.
 * Bound the retry so a bad current definition cannot create an erase loop. */
static int mount_locked(void)
{
    bool incompatible;
    int err = load_locked(&incompatible);
    if (!incompatible || !fs.ready) { return err; }
    LOG_WRN("Parameter format incompatible (%d): clearing settings partition %u", err, partition_id);
    err = nvs_clear(&fs);
    if (!err) { err = load_locked(&incompatible); }
    if (!err) { set_status(0, status.flags | DP_PARAM_STORAGE_RESET); }
    return err;
}

int dp_params_init(const struct dp_equip_cfg *cfg, uint8_t area_id)
{
    if (equipment) {
        return -EALREADY;
    }
    int err = dp_validate(cfg);
    if (err) {
        return err;
    }
    equipment = cfg;
    partition_id = area_id;
    zbus_chan_set_msg_sub_pool(&dp_param_events, &param_event_pool);
    for (size_t i = 0; i < cfg->point_count; ++i) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, i, &p_scratch);
        if (p->source != DP_SOURCE_PARAMETER) { continue; }
        defs[param_count++] = i;
    }
    for (size_t i = 0; i < param_count; ++i) {
        staged[i] = PARAM_DEF(i)->initial;
    }
    commit_cache(0, NULL);
    cache_initialized = true;
    err = mount_locked();
    if (err) {
        LOG_ERR("Parameter storage initialization failed (%d)", err);
        set_status(err, 0);
    }
    return err;
}

static int begin_transaction(bool allow_pending)
{
    if (!equipment) {
        return -EACCES;
    }
    if (k_current_get() == &k_sys_work_q.thread) {
        return -EWOULDBLOCK;
    }
    if (k_mutex_lock(&transaction_lock, K_NO_WAIT)) {
        return -EBUSY;
    }
    if (reboot_frozen) {
        k_mutex_unlock(&transaction_lock);
        return -EBUSY;
    }
    if (!allow_pending && (status.flags & DP_PARAM_PENDING)) {
        int err = -EBUSY;
        k_mutex_unlock(&transaction_lock);
        return err;
    }
    stage_cache();
    operation_flags = 0;
    return 0;
}

static int end_transaction(int err)
{
    notify_result(err);
    k_mutex_unlock(&transaction_lock);
    return err;
}

static int apply_staged(const bool selected[CONFIG_DP_PARAMS_MAX_COUNT])
{
    bool remote = false, local = false, persistent = false, changed = false;
    bool durable_remote = false, volatile_remote = false;
    uint16_t dev_id = 0, begin = UINT16_MAX;
    uint32_t end = 0;
    /* Validate and prepare the entire request before touching any device/flash. */
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const struct dp_point_cfg *p = PARAM_DEF(i);
        if (!(p->param_flags & DP_PARAM_WRITE)) {
            return -EACCES;
        }
        int err = range_check(p, &staged[i]);
        if (!err && p->prepare_write) {
            err = p->prepare_write(p, &staged[i], &cache[i].value);
        }
        if (!err) {
            err = range_check(p, &staged[i]);
        }
        if (err) {
            return err;
        }
        changed |= !(operation_flags & DP_OP_NO_DM) &&
                   memcmp(&cache[i].value, &staged[i], p->width * 2) != 0;
        bool store = (p->param_flags & DP_PARAM_PERSIST) &&
                     !(operation_flags & (DP_OP_NO_DM | DP_OP_NO_STORE));
        if (store && !(status.flags & DP_PARAM_STORAGE_READY)) { return -EROFS; }
        bool defer = (operation_flags & DP_OP_DEFER) || (p->param_flags & DP_PARAM_DEFER_STORE);
        persistent |= store && !defer;
        if (store && !defer) { durable_stage[i] = staged[i]; }
        if ((p->param_flags & DP_PARAM_FORWARD) && !(operation_flags & DP_OP_NO_COLLECT)) {
            if (remote && dev_id != p->key.dev_id) {
                return -EXDEV;
            }
            durable_remote |= (p->param_flags & DP_PARAM_PERSIST) != 0;
            volatile_remote |= (p->param_flags & DP_PARAM_PERSIST) == 0;
            remote = true;
            dev_id = p->key.dev_id;
            begin = MIN(begin, p->write_address);
            end = MAX(end, (uint32_t)p->write_address + p->width);
        } else {
            local = true;
        }
    }
    if ((remote && local) || (durable_remote && volatile_remote)) {
        return -EXDEV;
    }
    uint32_t sequence = status.sequence + (changed ? 1U : 0U);
    if (!remote) {
        int err = persistent ? save_parameters(sequence) : 0;
        if (err) {
            /* An I/O failure may have happened after metadata was written.
             * Freeze writes until remount resolves the durable state. */
            set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
            return err;
        }
        for (size_t i = 0; i < param_count; ++i) {
            if (!selected[i]) { continue; }
            if (operation_flags & DP_OP_NO_DM) { staged[i] = cache[i].value; }
            if ((PARAM_DEF(i)->param_flags & DP_PARAM_PERSIST) &&
                !(operation_flags & (DP_OP_NO_DM | DP_OP_NO_STORE))) {
                bool defer = (operation_flags & DP_OP_DEFER) ||
                             (PARAM_DEF(i)->param_flags & DP_PARAM_DEFER_STORE);
                dirty[i] = defer;
                if (defer) { deferred[i] = staged[i]; flush_at = k_uptime_get() + 1000; }
            }
        }
        commit_cache(sequence, selected);
        return 0;
    }
    size_t count = end - begin;
    if (!count || count > DP_TRANSFER_WORDS) {
        return -EXDEV;
    }
    bool covered[DP_TRANSFER_WORDS] = {0};
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const struct dp_point_cfg *p = PARAM_DEF(i);
        for (size_t w = 0; w < p->width; ++w) {
            size_t n = p->write_address - begin + w;
            covered[n] = true;
            new_wire[n] = dp_order_word(staged[i].words, p->width, w, p->write_order);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!covered[i]) {
            return -EXDEV;
        }
    }
    if (volatile_remote) {
        /* One command, one write. An uncertain ACK must not cause replay. */
        int err = transport_write(dev_id, begin, new_wire, count);
        if (err) { return err < 0 ? err : -EIO; }
        if (operation_flags & DP_OP_NO_DM) { stage_cache(); }
        commit_cache(sequence, selected);
        return 0;
    }
    if (!(status.flags & DP_PARAM_STORAGE_READY)) { return -EROFS; }
    uint8_t unit = unit_for(dev_id);
    int err = transport_read(dev_id, begin, old_wire, count);
    if (err) {
        return err < 0 ? err : -EIO;
    }
    size_t image_len = encode_changes(intent + INTENT_HEADER + 4 * count, sequence);
    sys_put_le32(INTENT_MAGIC, intent);
    sys_put_le16(FORMAT_VERSION, intent + 4);
    intent[6] = unit;
    intent[7] = count;
    sys_put_le16(begin, intent + 8);
    intent[10] = operation_flags;
    intent[11] = bus_for_device(dev_id);
    sys_put_le32(sequence, intent + 12);
    sys_put_le16(image_len, intent + 16);
    sys_put_le16(dev_id, intent + 18);
    for (size_t i = 0; i < count; ++i) {
        sys_put_le16(old_wire[i], intent + INTENT_HEADER + 2 * i);
        sys_put_le16(new_wire[i], intent + INTENT_HEADER + 2 * count + 2 * i);
    }
    intent_length = INTENT_HEADER + 4 * count + image_len;
    ssize_t rc = nvs_write(&fs, INTENT_ID, intent, intent_length);
    if (rc < 0) {
        set_status(rc, status.flags & ~DP_PARAM_STORAGE_READY);
        return rc; /* No downstream write without a durable intent. */
    }
    invalidate_pending(-EINPROGRESS);
    err = transport_write(dev_id, begin, new_wire, count);
    int recovered = recover_locked();
    if (recovered == 0) {
        return 0; /* Readback also resolves a lost write acknowledgement. */
    }
    if (recovered == 1) {
        return err < 0 ? err : -EIO;
    }
    invalidate_pending(recovered);
    return recovered;
}

int dp_params_apply(const struct dp_point_update *values, size_t count, uint8_t flags)
{
    if (!values || !count || count > CONFIG_DP_PARAMS_MAX_COUNT || (flags & ~DP_OP_MASK)) {
        return -EINVAL;
    }
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    operation_flags = flags;
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    for (size_t i = 0; i < count; ++i) {
        int index = index_of(values[i].key);
        if (index < 0) {
            return end_transaction(index);
        }
        if (selected[index] || values[i].error || values[i].width != PARAM_DEF(index)->width) {
            return end_transaction(-EINVAL);
        }
        staged[index] = values[i].value;
        selected[index] = true;
    }
    return end_transaction(apply_staged(selected));
}

int dp_params_set_values(const struct dp_point_update *values, size_t count)
{
    return dp_params_apply(values, count, 0);
}

int dp_params_write_registers(const struct dp_equip_cfg *mapping, uint16_t address,
                               const uint16_t *words, size_t count)
{
    if (!mapping || !words || !count || count > DP_TRANSFER_WORDS || (uint32_t)address + count > 65536) {
        return -EINVAL;
    }
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    size_t offset = 0;
    while (offset < count) {
        const struct dp_transmit_cfg *map = NULL;
        struct dp_transmit_cfg t_scratch;
        for (size_t i = 0; i < mapping->transmit_count; ++i) {
            const struct dp_transmit_cfg *t = dp_config_transmit(mapping, i, &t_scratch);
            if (!t) { return end_transaction(-EINVAL); }
            if (t->function == 3 && t->address == address + offset && t->kind == DP_MAP_VALUE) {
                map = t;
                break;
            }
        }
        int index = map ? index_of(map->key) : -ENOENT;
        if (index < 0) {
            return end_transaction(-ENOENT);
        }
        if (map->count != PARAM_DEF(index)->width || offset + map->count > count || selected[index]) {
            return end_transaction(-EINVAL); /* No partial multiword/duplicate alias writes. */
        }
        selected[index] = true;
        for (size_t w = 0; w < map->count; ++w) {
            staged[index].words[w] = dp_order_word(words + offset, map->count, w, map->order);
        }
        offset += map->count;
    }
    return end_transaction(apply_staged(selected));
}

int dp_params_restore(uint16_t dev_id)
{
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    bool found = false;
    for (size_t i = 0; i < param_count; ++i) {
        if (PARAM_DEF(i)->key.dev_id == dev_id && (PARAM_DEF(i)->param_flags & DP_PARAM_RESTORE)) {
            err = dp_reference_default(PARAM_DEF(i), &staged[i], true);
            if (err) { return end_transaction(err); }
            selected[i] = true;
            found = true;
        }
    }
    return end_transaction(found ? apply_staged(selected) : -ENOENT);
}

static int flush_for_reboot(bool freeze)
{
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    if (!(status.flags & DP_PARAM_STORAGE_READY)) { return end_transaction(-EROFS); }
    for (size_t i = 0; i < param_count; ++i) {
        if (dirty[i]) { durable_stage[i] = deferred[i]; }
    }
    err = save_parameters(status.sequence);
    if (!err) { memset(dirty, 0, sizeof(dirty)); flush_at = 0; reboot_frozen = freeze; }
    if (err) {
        set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
    }
    return end_transaction(err);
}

int dp_params_flush(void) { return flush_for_reboot(false); }
int dp_params_prepare_reboot(void) { return flush_for_reboot(true); }
void dp_params_cancel_reboot(void)
{
    k_mutex_lock(&transaction_lock, K_FOREVER);
    reboot_frozen = false;
    k_mutex_unlock(&transaction_lock);
}

int dp_params_recover(void)
{
    int err = begin_transaction(true);
    if (err) {
        return err;
    }
    if (!(status.flags & DP_PARAM_STORAGE_READY)) { return end_transaction(-EROFS); }
    err = recover_locked();
    if (err < 0 && (status.flags & DP_PARAM_PENDING)) {
        invalidate_pending(err);
    }
    return end_transaction(err < 0 ? err : 0);
}

int dp_params_reload(void)
{
    if (!equipment || k_current_get() == &k_sys_work_q.thread) {
        return -EACCES;
    }
    if (k_mutex_lock(&transaction_lock, K_NO_WAIT)) {
        return -EBUSY;
    }
    if (reboot_frozen) { k_mutex_unlock(&transaction_lock); return -EBUSY; }
    int err = mount_locked();
    if (err) {
        set_status(err, 0);
    }
    return end_transaction(err);
}

int dp_params_flush_due(void)
{
    if (!equipment || k_mutex_lock(&transaction_lock, K_NO_WAIT)) { return -EBUSY; }
    bool due = flush_at && k_uptime_get() >= flush_at;
    k_mutex_unlock(&transaction_lock);
    return due ? dp_params_flush() : 0;
}

int dp_params_sync_device(uint16_t dev_id)
{
    if (!equipment) { return -EACCES; }
    /* Explicit initialization operation, never automatic crash recovery replay. */
    for (size_t i = 0; i < param_count; ++i) {
        if (PARAM_DEF(i)->key.dev_id != dev_id ||
            (PARAM_DEF(i)->param_flags & (DP_PARAM_FORWARD | DP_PARAM_PERSIST)) !=
            (DP_PARAM_FORWARD | DP_PARAM_PERSIST)) { continue; }
        int err = begin_transaction(false);
        if (err) { return err; }
        /* A polled live value must not replace the desired configuration. */
        staged[i] = dirty[i] ? deferred[i] : durable[i];
        bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
        selected[i] = true;
        operation_flags = DP_OP_NO_DM | DP_OP_NO_STORE;
        err = end_transaction(apply_staged(selected));
        if (err) { return err; }
    }
    return 0;
}

/* Poll feedback only updates live values. It neither stores nor sends a command. */
int dp_params_sample(const struct dp_point_update *values, size_t count,
                     uint16_t flags, int64_t sampled_at)
{
    if (!equipment || !values || !count || count > CONFIG_DP_PARAMS_MAX_COUNT ||
        (flags & ~DP_QUALITY_SIMULATED) || sampled_at < 0 ||
        sampled_at > k_uptime_get() || k_is_in_isr()) { return -EINVAL; }
    if (k_mutex_lock(&transaction_lock, K_NO_WAIT)) { return -EBUSY; }
    if (status.flags & DP_PARAM_PENDING) {
        k_mutex_unlock(&transaction_lock); return -EBUSY;
    }
    for (size_t i = 0; i < count; ++i) {
        int n = index_of(values[i].key);
        if (n < 0 || !(PARAM_DEF(n)->param_flags & DP_PARAM_FORWARD) ||
            values[i].width != PARAM_DEF(n)->width) {
            k_mutex_unlock(&transaction_lock); return -EINVAL;
        }
    }
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < count; ++i) {
        int n = index_of(values[i].key);
        if (sampled_at <= cache[n].sampled_at ||
            !dp_acquired_sample_current(values[i].key.dev_id, sampled_at)) { continue; }
        int err = values[i].error;
        uint16_t quality = err ? DP_QUALITY_COMM_ERROR : DP_QUALITY_GOOD;
        if (!err) {
            err = range_check(PARAM_DEF(n), &values[i].value);
            if (err) { quality = DP_QUALITY_BAD_VALUE; }
        }
        if (!err) {
            dp_reference_notify(PARAM_DEF(n), &cache[n].value, &values[i].value);
            cache[n].value = values[i].value;
        }
        cache[n].error = err;
        cache[n].quality = quality | flags;
        cache[n].sampled_at = sampled_at;
        ++cache[n].generation;
    }
    k_mutex_unlock(&cache_lock);
    k_mutex_unlock(&transaction_lock);
    return 0;
}

void dp_params_invalidate_acquired_device(uint16_t dev, int error, int64_t observed_at)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        const struct dp_point_cfg *p = PARAM_DEF(i);
        if (p->key.dev_id != dev || !dp_acquired_key(equipment, p->key) ||
            cache[i].sampled_at > observed_at) { continue; }
        cache[i].quality = (cache[i].quality & DP_QUALITY_SIMULATED) | DP_QUALITY_COMM_ERROR;
        cache[i].error = error;
        ++cache[i].generation;
    }
    k_mutex_unlock(&cache_lock);
}
