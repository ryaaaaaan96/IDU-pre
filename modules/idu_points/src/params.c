#include <idu/params.h>
#include <idu/process.h>
#include <idu/collect.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(idu_params, LOG_LEVEL_INF);

#define SNAP_ID 1
#define INTENT_ID 2
#define SNAP_MAGIC 0x50554449U
#define INTENT_MAGIC 0x4a554449U
#define FORMAT_VERSION 1
#define SNAP_HEADER 16
#define INTENT_HEADER 20
#define SNAP_MAX CONFIG_IDU_PARAMS_SNAPSHOT_BYTES
#define INTENT_MAX (INTENT_HEADER + 4 * IDU_POLL_REGS + SNAP_MAX)

static const struct idu_equip_cfg *equipment;
static const struct idu_point_cfg *defs[CONFIG_IDU_PARAMS_MAX_COUNT];
static struct idu_point_state cache[CONFIG_IDU_PARAMS_MAX_COUNT];
static size_t param_count;
static struct idu_value staged[CONFIG_IDU_PARAMS_MAX_COUNT];
/* durable never follows transient NO_STORE cache writes. */
static struct idu_value durable[CONFIG_IDU_PARAMS_MAX_COUNT];
static struct idu_value durable_stage[CONFIG_IDU_PARAMS_MAX_COUNT];
static struct idu_value deferred[CONFIG_IDU_PARAMS_MAX_COUNT];
static bool dirty[CONFIG_IDU_PARAMS_MAX_COUNT];
static int64_t flush_at;
static uint8_t operation_flags;
static struct nvs_fs fs;
static const struct flash_area *area;
static uint8_t partition_id;
static bool virtual_source;
static uint32_t schema, previous_schema;
static bool has_added_params;
static bool migration_pending;
static struct idu_param_status status;
/* Scratch space is serialized by transaction_lock, never borrowed by readers. */
static uint8_t snapshot[SNAP_MAX];
static uint8_t intent[INTENT_MAX];
static size_t intent_length;
static uint16_t old_wire[IDU_POLL_REGS], new_wire[IDU_POLL_REGS], actual[IDU_POLL_REGS];
K_MUTEX_DEFINE(transaction_lock);
static bool reboot_frozen;
K_MUTEX_DEFINE(cache_lock);
NET_BUF_POOL_FIXED_DEFINE(param_event_pool, 2, sizeof(struct idu_param_status),
                         sizeof(struct zbus_channel *), NULL);
ZBUS_CHAN_DEFINE(idu_param_events, struct idu_param_status, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

static bool simulated_device(uint16_t dev_id)
{
    struct idu_device_status device;
    return idu_collect_device_status(dev_id, &device) ? virtual_source : device.simulated;
}

static int range_check(const struct idu_point_cfg *p, const struct idu_value *v)
{
    int64_t value;
    uint32_t u32 = v->words[0] | ((uint32_t)v->words[1] << 16);
    switch (p->type) {
    case IDU_U8:
    case IDU_U16: value = v->words[0]; break;
    case IDU_S16: value = (int16_t)v->words[0]; break;
    case IDU_U32: value = u32; break;
    case IDU_S32: value = (int32_t)u32; break;
    case IDU_WORDS: return 0;
    default: return -EINVAL;
    }
    if (p->type == IDU_U8 && value > UINT8_MAX) { return -ERANGE; }
    return value < p->minimum || value > p->maximum ? -ERANGE : 0;
}

int idu_params_validate(const struct idu_equip_cfg *cfg)
{
    size_t count = 0, bytes = SNAP_HEADER;
    for (size_t i = 0; i < cfg->point_count; ++i) {
        const struct idu_point_cfg *p = &cfg->points[i];
        if (p->source != IDU_SOURCE_PARAMETER) {
            if (p->param_flags || p->prepare_write || p->param_added || p->seed_missing) {
                return -EINVAL;
            }
            continue;
        }
        if (++count > CONFIG_IDU_PARAMS_MAX_COUNT || p->process || p->stale_ms ||
            (p->param_flags & ~0x1fU) || range_check(p, &p->initial) ||
            ((p->param_flags & IDU_PARAM_RESTORE) && !(p->param_flags & IDU_PARAM_WRITE)) ||
            ((p->param_added || p->seed_missing) && !(p->param_flags & IDU_PARAM_PERSIST))) {
            LOG_ERR("Invalid parameter %u:%u count=%u flags=%u",p->key.dev_id,p->key.sig_id,(unsigned)count,p->param_flags);
            return -EINVAL;
        }
        if (p->param_flags & IDU_PARAM_PERSIST) { bytes += 6 + p->width * 2; }
        if (bytes > SNAP_MAX) { return -E2BIG; }
        if (p->param_flags & IDU_PARAM_FORWARD) {
            bool found = false;
            for (size_t d = 0; d < cfg->device_count; ++d) {
                found |= cfg->devices[d].dev_id == p->key.dev_id;
            }
            if (!found || !(p->param_flags & IDU_PARAM_PERSIST) ||
                p->write_order > IDU_HIGH_WORD_BYTE_SWAP ||
                (uint32_t)p->write_address + p->width > 65536) {
                return -EINVAL;
            }
            for (size_t j = 0; j < i; ++j) {
                const struct idu_point_cfg *q = &cfg->points[j];
                if (q->source == IDU_SOURCE_PARAMETER &&
                    (q->param_flags & IDU_PARAM_FORWARD) && q->key.dev_id == p->key.dev_id &&
                    q->write_address < (uint32_t)p->write_address + p->width &&
                    p->write_address < (uint32_t)q->write_address + q->width) {
                    return -EEXIST;
                }
            }
        }
    }
    return 0;
}

static int index_of(struct idu_key key)
{
    for (size_t i = 0; i < param_count; ++i) {
        if (defs[i]->key.dev_id == key.dev_id && defs[i]->key.sig_id == key.sig_id) {
            return i;
        }
    }
    return -ENOENT;
}

void idu_params_get_status(struct idu_param_status *out)
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
    struct idu_param_status event;
    k_mutex_lock(&cache_lock, K_FOREVER);
    status.error = error;
    event = status;
    k_mutex_unlock(&cache_lock);
    int err = zbus_chan_pub(&idu_param_events, &event, K_NO_WAIT);
    if (err) {
        LOG_WRN("Parameter notification lost: %d", err);
    }
}

int idu_params_read(struct idu_key key, struct idu_point_state *out)
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

static void commit_cache(uint32_t sequence)
{
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        cache[i].value = staged[i];
        cache[i].quality = ((defs[i]->param_flags & IDU_PARAM_FORWARD) && simulated_device(defs[i]->key.dev_id)) ?
                            IDU_QUALITY_SIMULATED : IDU_QUALITY_GOOD;
        cache[i].sampled_at = k_uptime_get();
        cache[i].generation = sequence;
        cache[i].error = 0;
    }
    status.sequence = sequence;
    k_mutex_unlock(&cache_lock);
}

static size_t encode_snapshot(uint8_t *buf, uint32_t sequence)
{
    size_t offset = SNAP_HEADER;
    uint16_t count = 0;
    sys_put_le32(SNAP_MAGIC, buf);
    sys_put_le16(FORMAT_VERSION, buf + 4);
    sys_put_le32(schema, buf + 8);
    sys_put_le32(sequence, buf + 12);
    for (size_t i = 0; i < param_count; ++i) {
        const struct idu_point_cfg *p = defs[i];
        if (!(p->param_flags & IDU_PARAM_PERSIST)) {
            continue;
        }
        sys_put_le16(p->key.dev_id, buf + offset);
        sys_put_le16(p->key.sig_id, buf + offset + 2);
        buf[offset + 4] = p->type;
        buf[offset + 5] = p->width;
        offset += 6;
        for (size_t w = 0; w < p->width; ++w) {
            sys_put_le16(durable_stage[i].words[w], buf + offset);
            offset += 2;
        }
        ++count;
    }
    sys_put_le16(count, buf + 6);
    return offset;
}

static int decode_snapshot(const uint8_t *buf, size_t len, uint32_t *sequence)
{
    bool previous = len >= SNAP_HEADER && has_added_params &&
                    sys_get_le32(buf + 8) == previous_schema;
    if (len < SNAP_HEADER || sys_get_le32(buf) != SNAP_MAGIC ||
        sys_get_le16(buf + 4) != FORMAT_VERSION ||
        (sys_get_le32(buf + 8) != schema && !previous)) {
        return -EPROTO;
    }
    size_t offset = SNAP_HEADER;
    uint16_t count = 0;
    for (size_t i = 0; i < param_count; ++i) {
        const struct idu_point_cfg *p = defs[i];
        if (!(p->param_flags & IDU_PARAM_PERSIST) || (previous && p->param_added)) {
            continue;
        }
        if (offset + 6 + 2 * p->width > len ||
            sys_get_le16(buf + offset) != p->key.dev_id ||
            sys_get_le16(buf + offset + 2) != p->key.sig_id ||
            buf[offset + 4] != p->type || buf[offset + 5] != p->width) {
            return -EPROTO;
        }
        offset += 6;
        for (size_t w = 0; w < p->width; ++w) {
            durable_stage[i].words[w] = sys_get_le16(buf + offset);
            offset += 2;
        }
        if (range_check(p, &durable_stage[i])) {
            return -ERANGE;
        }
        ++count;
    }
    if (offset != len || count != sys_get_le16(buf + 6)) {
        return -EPROTO;
    }
    *sequence = sys_get_le32(buf + 12);
    return 0;
}

static int save_snapshot(uint32_t sequence)
{
    size_t len = encode_snapshot(snapshot, sequence);
    ssize_t rc = nvs_write(&fs, SNAP_ID, snapshot, len);
    if (rc >= 0) {
        memcpy(durable, durable_stage, sizeof(durable));
        migration_pending = false;
    }
    return rc < 0 ? (int)rc : 0; /* NVS returns 0 when unchanged. */
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
        (intent[10] & ~IDU_OP_MASK) || intent[11] != bus_for_device(sys_get_le16(intent + 18)) || intent[7] > IDU_POLL_REGS || intent[6] != unit_for(sys_get_le16(intent + 18)) ||
        intent[6] == 0 || (uint32_t)sys_get_le16(intent + 8) + intent[7] > 65536 ||
        intent_length != INTENT_HEADER + 4U * intent[7] + sys_get_le16(intent + 16)) {
        return -EPROTO;
    }
    uint32_t sequence;
    int err = decode_snapshot(intent + INTENT_HEADER + 4U * intent[7],
                               sys_get_le16(intent + 16), &sequence);
    return err ? err : sequence == sys_get_le32(intent + 12) ? 0 : -EPROTO;
}

static void invalidate_pending(int error)
{
    uint16_t dev_id = sys_get_le16(intent + 18);
    uint16_t address = sys_get_le16(intent + 8);
    k_mutex_lock(&cache_lock, K_FOREVER);
    for (size_t i = 0; i < param_count; ++i) {
        if ((defs[i]->param_flags & IDU_PARAM_FORWARD) && defs[i]->key.dev_id == dev_id &&
            defs[i]->write_address >= address &&
            defs[i]->write_address < (uint32_t)address + intent[7]) {
            cache[i].quality = IDU_QUALITY_COMM_ERROR |
                               (simulated_device(dev_id) ? IDU_QUALITY_SIMULATED : 0);
            cache[i].error = error;
        }
    }
    status.flags |= IDU_PARAM_PENDING;
    k_mutex_unlock(&cache_lock);
}

/* Returns 0 if committed, 1 if the device still has the pre-write values.
 * Neither timeout nor an ambiguous result triggers an automatic write retry.
 */
static int recover_locked(void)
{
    if (!(status.flags & IDU_PARAM_PENDING)) {
        return 0;
    }
    stage_cache();
    int err = intent_validate();
    if (err) {
        return err;
    }
    size_t count = intent[7];
    err = idu_collect_read(sys_get_le16(intent + 18), 3, sys_get_le16(intent + 8), actual, count);
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
        err = save_snapshot(sequence);
        if (err) {
            return err;
        }
        uint8_t flags = sys_get_le16(intent + 10);
        uint16_t dev = sys_get_le16(intent + 18), begin = sys_get_le16(intent + 8);
        for (size_t i = 0; i < param_count; ++i) {
            const struct idu_point_cfg *p = defs[i];
            if (!(p->param_flags & IDU_PARAM_FORWARD) || p->key.dev_id != dev ||
                p->write_address < begin || (uint32_t)p->write_address + p->width > begin + count) {
                continue;
            }
            if (!(flags & IDU_OP_NO_DM)) {
                for (size_t w = 0; w < p->width; ++w) {
                    staged[i].words[w] = idu_order_word(actual + p->write_address - begin,
                                                        p->width, w, p->write_order);
                }
                if (!(flags & IDU_OP_NO_STORE)) {
                    bool defer = (flags & IDU_OP_DEFER) || (p->param_flags & IDU_PARAM_DEFER_STORE);
                    dirty[i] = defer;
                    if (defer) { deferred[i] = staged[i]; flush_at = k_uptime_get() + 1000; }
                }
            }
        }
        commit_cache(sequence);
    }
    if (!is_new && migration_pending) {
        stage_cache();
        err = save_snapshot(status.sequence);
        if (err) { return err; }
    }
    err = nvs_delete(&fs, INTENT_ID);
    if (err < 0) {
        return err;
    }
    if (!is_new) {
        stage_cache(); /* Clear the uncertain quality without applying the candidate. */
        commit_cache(status.sequence);
    }
    intent_length = 0;
    set_status(0, status.flags & ~IDU_PARAM_PENDING);
    return is_new ? 0 : 1;
}

static int mount_locked(void)
{
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
        staged[i] = durable_stage[i] = defs[i]->initial;
    }
    uint32_t sequence = 0;
    bool recovered = false, empty = false, migrate = false;
    for (uint16_t history = 0; history < 8; ++history) {
        ssize_t len = nvs_read_hist(&fs, SNAP_ID, snapshot, sizeof(snapshot), history);
        if (len == -ENOENT && history == 0) {
            empty = true;
            break; /* New empty partition, use defaults. */
        }
        if (len == -EIO) {
            recovered = true;
            if (history == 7) {
                return -EIO;
            }
            continue;
        }
        if (len < 0 || len > sizeof(snapshot)) {
            return len < 0 ? len : -EMSGSIZE;
        }
        err = decode_snapshot(snapshot, len, &sequence);
        if (err) {
            return err; /* Unknown schema is not reformatted or silently reset. */
        }
        migrate = sys_get_le32(snapshot + 8) != schema;
        break;
    }
    for (size_t i = 0; i < param_count; ++i) {
        const struct idu_point_cfg *p = defs[i];
        if ((empty || (migrate && p->param_added)) && p->seed_missing) {
            err = p->seed_missing(p, &durable_stage[i]);
            if (!err) { err = range_check(p, &durable_stage[i]); }
            if (err) { return err; }
        }
    }
    migration_pending = empty || migrate;
    memcpy(staged, durable_stage, sizeof(staged));
    memcpy(durable, durable_stage, sizeof(durable));
    memset(dirty, 0, sizeof(dirty));
    flush_at = 0;
    commit_cache(sequence);
    ssize_t len = nvs_read(&fs, INTENT_ID, intent, sizeof(intent));
    intent_length = 0;
    if (len != -ENOENT) {
        if (len < 0 || len > sizeof(intent)) {
            return len < 0 ? len : -EMSGSIZE;
        }
        intent_length = len;
        stage_cache();
        err = intent_validate();
        if (err) {
            return err;
        }
    }
    /* Validate the intent before writing anything. With a pending transaction,
     * recovery upgrades its snapshot after readback; never replay the write.
     * Do not save its candidate here as the current committed configuration. */
    if ((empty || migrate) && !intent_length) {
        err = save_snapshot(sequence);
        if (err) { return err; }
    }
    set_status(0, IDU_PARAM_STORAGE_READY |
               (recovered ? IDU_PARAM_HISTORY_RECOVERED : 0) |
               (migrate ? IDU_PARAM_SCHEMA_MIGRATED : 0) |
               (intent_length ? IDU_PARAM_PENDING : 0));
    if (intent_length) {
        invalidate_pending(-EINPROGRESS);
    }
    return 0;
}

int idu_params_init(const struct idu_equip_cfg *cfg, uint8_t area_id,
                    int client_iface, bool simulated)
{
    if (equipment) {
        return -EALREADY;
    }
    int err = idu_points_validate(cfg);
    if (err || client_iface < 0) {
        return err ? err : -EINVAL;
    }
    equipment = cfg;
    partition_id = area_id;
    ARG_UNUSED(client_iface);
    virtual_source = simulated;
    zbus_chan_set_msg_sub_pool(&idu_param_events, &param_event_pool);
    for (size_t i = 0; i < cfg->point_count; ++i) {
        if (cfg->points[i].source != IDU_SOURCE_PARAMETER) {
            continue;
        }
        const struct idu_point_cfg *p = &cfg->points[i];
        defs[param_count++] = p;
        uint8_t signature[12] = {0};
        sys_put_le16(p->key.dev_id, signature);
        sys_put_le16(p->key.sig_id, signature + 2);
        signature[4] = p->type;
        signature[5] = p->width;
        signature[6] = p->param_flags;
        signature[7] = p->write_order;
        sys_put_le16(p->write_address, signature + 8);
        schema = crc32_ieee_update(schema, signature, sizeof(signature));
        if (p->param_added) { has_added_params = true; }
        else { previous_schema = crc32_ieee_update(previous_schema, signature, sizeof(signature)); }
    }
    for (size_t i = 0; i < param_count; ++i) {
        staged[i] = defs[i]->initial;
    }
    commit_cache(0);
    err = mount_locked();
    if (err) {
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
    if (!(status.flags & IDU_PARAM_STORAGE_READY) ||
        (!allow_pending && (status.flags & IDU_PARAM_PENDING))) {
        int err = status.flags & IDU_PARAM_PENDING ? -EBUSY : -EROFS;
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

static int apply_staged(const bool selected[CONFIG_IDU_PARAMS_MAX_COUNT])
{
    bool remote = false, local = false, persistent = false, changed = false;
    uint16_t dev_id = 0, begin = UINT16_MAX;
    uint32_t end = 0;
    /* Validate and prepare the entire request before touching any device/flash. */
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const struct idu_point_cfg *p = defs[i];
        if (!(p->param_flags & IDU_PARAM_WRITE)) {
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
        changed |= !(operation_flags & IDU_OP_NO_DM) &&
                   memcmp(&cache[i].value, &staged[i], p->width * 2) != 0;
        bool store = (p->param_flags & IDU_PARAM_PERSIST) &&
                     !(operation_flags & (IDU_OP_NO_DM | IDU_OP_NO_STORE));
        bool defer = (operation_flags & IDU_OP_DEFER) || (p->param_flags & IDU_PARAM_DEFER_STORE);
        persistent |= store && !defer;
        if (store && !defer) { durable_stage[i] = staged[i]; }
        if ((p->param_flags & IDU_PARAM_FORWARD) && !(operation_flags & IDU_OP_NO_COLLECT)) {
            if (remote && dev_id != p->key.dev_id) {
                return -EXDEV;
            }
            remote = true;
            dev_id = p->key.dev_id;
            begin = MIN(begin, p->write_address);
            end = MAX(end, (uint32_t)p->write_address + p->width);
        } else {
            local = true;
        }
    }
    if (remote && local) {
        return -EXDEV;
    }
    uint32_t sequence = status.sequence + (changed ? 1U : 0U);
    if (!remote) {
        int err = persistent ? save_snapshot(sequence) : 0;
        if (err) {
            /* An I/O failure may have happened after metadata was written.
             * Freeze writes until remount resolves the durable state. */
            set_status(err, status.flags & ~IDU_PARAM_STORAGE_READY);
            return err;
        }
        for (size_t i = 0; i < param_count; ++i) {
            if (!selected[i]) { continue; }
            if (operation_flags & IDU_OP_NO_DM) { staged[i] = cache[i].value; }
            if ((defs[i]->param_flags & IDU_PARAM_PERSIST) &&
                !(operation_flags & (IDU_OP_NO_DM | IDU_OP_NO_STORE))) {
                bool defer = (operation_flags & IDU_OP_DEFER) ||
                             (defs[i]->param_flags & IDU_PARAM_DEFER_STORE);
                dirty[i] = defer;
                if (defer) { deferred[i] = staged[i]; flush_at = k_uptime_get() + 1000; }
            }
        }
        commit_cache(sequence);
        return 0;
    }
    size_t count = end - begin;
    if (!count || count > IDU_POLL_REGS) {
        return -EXDEV;
    }
    bool covered[IDU_POLL_REGS] = {0};
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const struct idu_point_cfg *p = defs[i];
        for (size_t w = 0; w < p->width; ++w) {
            size_t n = p->write_address - begin + w;
            covered[n] = true;
            new_wire[n] = idu_order_word(staged[i].words, p->width, w, p->write_order);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!covered[i]) {
            return -EXDEV;
        }
    }
    uint8_t unit = unit_for(dev_id);
    int err = idu_collect_read(dev_id, 3, begin, old_wire, count);
    if (err) {
        return err < 0 ? err : -EIO;
    }
    size_t image_len = encode_snapshot(intent + INTENT_HEADER + 4 * count, sequence);
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
        set_status(rc, status.flags & ~IDU_PARAM_STORAGE_READY);
        return rc; /* No downstream write without a durable intent. */
    }
    invalidate_pending(-EINPROGRESS);
    err = idu_collect_write(dev_id, begin, new_wire, count);
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

int idu_params_apply(const struct idu_point_update *values, size_t count, uint8_t flags)
{
    if (!values || !count || count > CONFIG_IDU_PARAMS_MAX_COUNT || (flags & ~IDU_OP_MASK)) {
        return -EINVAL;
    }
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    operation_flags = flags;
    bool selected[CONFIG_IDU_PARAMS_MAX_COUNT] = {0};
    for (size_t i = 0; i < count; ++i) {
        int index = index_of(values[i].key);
        if (index < 0) {
            return end_transaction(index);
        }
        if (selected[index] || values[i].error || values[i].width != defs[index]->width) {
            return end_transaction(-EINVAL);
        }
        staged[index] = values[i].value;
        selected[index] = true;
    }
    return end_transaction(apply_staged(selected));
}

int idu_params_set_values(const struct idu_point_update *values, size_t count)
{
    return idu_params_apply(values, count, 0);
}

int idu_params_write_registers(uint16_t address, const uint16_t *words, size_t count)
{
    if (!words || !count || count > IDU_POLL_REGS || (uint32_t)address + count > 65536) {
        return -EINVAL;
    }
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    bool selected[CONFIG_IDU_PARAMS_MAX_COUNT] = {0};
    size_t offset = 0;
    while (offset < count) {
        const struct idu_transmit_cfg *map = NULL;
        for (size_t i = 0; i < equipment->transmit_count; ++i) {
            const struct idu_transmit_cfg *t = &equipment->transmit[i];
            if (t->function == 3 && t->address == address + offset && t->kind == IDU_MAP_VALUE) {
                map = t;
                break;
            }
        }
        int index = map ? index_of(map->key) : -ENOENT;
        if (index < 0) {
            return end_transaction(-ENOENT);
        }
        if (map->count != defs[index]->width || offset + map->count > count || selected[index]) {
            return end_transaction(-EINVAL); /* No partial multiword/duplicate alias writes. */
        }
        selected[index] = true;
        for (size_t w = 0; w < map->count; ++w) {
            staged[index].words[w] = idu_order_word(words + offset, map->count, w, map->order);
        }
        offset += map->count;
    }
    return end_transaction(apply_staged(selected));
}

int idu_params_restore(uint16_t dev_id)
{
    int err = begin_transaction(false);
    if (err) {
        return err;
    }
    bool selected[CONFIG_IDU_PARAMS_MAX_COUNT] = {0};
    bool found = false;
    for (size_t i = 0; i < param_count; ++i) {
        if (defs[i]->key.dev_id == dev_id && (defs[i]->param_flags & IDU_PARAM_RESTORE)) {
            staged[i] = defs[i]->initial;
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
    for (size_t i = 0; i < param_count; ++i) {
        if (dirty[i]) { durable_stage[i] = deferred[i]; }
    }
    err = save_snapshot(status.sequence);
    if (!err) { memset(dirty, 0, sizeof(dirty)); flush_at = 0; reboot_frozen = freeze; }
    if (err) {
        set_status(err, status.flags & ~IDU_PARAM_STORAGE_READY);
    }
    return end_transaction(err);
}

int idu_params_flush(void) { return flush_for_reboot(false); }
int idu_params_prepare_reboot(void) { return flush_for_reboot(true); }
void idu_params_cancel_reboot(void)
{
    k_mutex_lock(&transaction_lock, K_FOREVER);
    reboot_frozen = false;
    k_mutex_unlock(&transaction_lock);
}

int idu_params_recover(void)
{
    int err = begin_transaction(true);
    if (err) {
        return err;
    }
    err = recover_locked();
    if (err < 0 && (status.flags & IDU_PARAM_PENDING)) {
        invalidate_pending(err);
    }
    return end_transaction(err < 0 ? err : 0);
}

int idu_params_reload(void)
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

int idu_params_flush_due(void)
{
    if (!equipment || k_mutex_lock(&transaction_lock, K_NO_WAIT)) { return -EBUSY; }
    bool due = flush_at && k_uptime_get() >= flush_at;
    k_mutex_unlock(&transaction_lock);
    return due ? idu_params_flush() : 0;
}

int idu_params_sync_device(uint16_t dev_id)
{
    if (!equipment) { return -EACCES; }
    /* Explicit initialization operation, never automatic crash recovery replay. */
    for (size_t i = 0; i < param_count; ++i) {
        if (defs[i]->key.dev_id != dev_id || !(defs[i]->param_flags & IDU_PARAM_FORWARD)) { continue; }
        struct idu_point_state state;
        int err = idu_params_read(defs[i]->key, &state);
        if (err) { return err; }
        struct idu_point_update value = {.key = defs[i]->key, .width = defs[i]->width,
                                         .value = state.value};
        err = idu_params_apply(&value, 1, IDU_OP_NO_DM | IDU_OP_NO_STORE);
        if (err) { return err; }
    }
    return 0;
}
