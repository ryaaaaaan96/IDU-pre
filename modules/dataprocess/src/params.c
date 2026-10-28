#include <dataprocess/params.h>
#include "dp_internal.h"
#include "parameter_store.h"
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(dp_params, LOG_LEVEL_INF);

#define IMAGE_HEADER DP_STORE_IMAGE_HEADER
#define IMAGE_MAX CONFIG_DP_PARAMS_TRANSACTION_BYTES

static size_t param_count;
static uint16_t defs[CONFIG_DP_PARAMS_MAX_COUNT];
static bool stored[CONFIG_DP_PARAMS_MAX_COUNT];
static struct nvs_fs fs;
static uint8_t partition_id;
static struct dp_param_status status;
static bool configured;
static bool ready;
static K_MUTEX_DEFINE(params_lock);
static K_MUTEX_DEFINE(status_lock);

struct param_buffers {
    struct dp_point_state cache[CONFIG_DP_PARAMS_MAX_COUNT];
    struct dp_value staged[CONFIG_DP_PARAMS_MAX_COUNT];
    struct dp_value durable[CONFIG_DP_PARAMS_MAX_COUNT];
    struct dp_value durable_stage[CONFIG_DP_PARAMS_MAX_COUNT];
    uint8_t image[IMAGE_MAX];
};

static struct param_buffers *buffers;

static int ensure_mounted(void);

static const struct dp_point_cfg *param(size_t index, struct dp_point_cfg *scratch)
{
    return dp_config_point(defs[index], scratch);
}

static int index_of(struct dp_key key)
{
    for (size_t i = 0; i < param_count; ++i) {
        struct dp_point_cfg p;
        if (!param(i, &p)) { continue; }
        if (p.key.dev_id == key.dev_id && p.key.sig_id == key.sig_id) { return i; }
    }
    return -ENOENT;
}

static void set_status(int error, uint16_t flags)
{
    k_mutex_lock(&status_lock, K_FOREVER);
    status.error = error;
    status.flags = flags;
    if (flags & DP_PARAM_STORAGE_READY) { status.storage_error = 0; }
    else if (error) { status.storage_error = error; }
    k_mutex_unlock(&status_lock);
}

static void commit_cache(uint32_t sequence, const bool *selected)
{
    for (size_t i = 0; i < param_count; ++i) {
        if (selected && !selected[i]) { continue; }
        buffers->cache[i].value = buffers->staged[i];
        buffers->cache[i].quality = DP_QUALITY_GOOD;
        buffers->cache[i].sampled_at = k_uptime_get();
        buffers->cache[i].generation = sequence;
        buffers->cache[i].error = 0;
    }
    status.sequence = sequence;
}

static int load_value(const struct dp_point_cfg *p, uint8_t type, uint8_t width,
                      const uint8_t *bytes, struct dp_value *value)
{
    if (type != p->type || width != p->width) { return -EPROTO; }
    *value = (struct dp_value){0};
    for (size_t w = 0; w < width; ++w) {
        value->words[w] = sys_get_le16(bytes + w * 2);
    }
    return dp_check_value(p, value);
}

static size_t encode_changes(uint8_t *buf, uint32_t sequence)
{
    size_t offset = IMAGE_HEADER;
    uint16_t count = 0;
    sys_put_le32(DP_STORE_IMAGE_MAGIC, buf);
    sys_put_le16(DP_STORE_VERSION, buf + 4);
    sys_put_le32(0, buf + 8);
    sys_put_le32(sequence, buf + 12);
    for (size_t i = 0; i < param_count; ++i) {
        struct dp_point_cfg p;
        if (!param(i, &p) || (p.flags & FLAG_PERSISTENCE) == 0) { continue; }
        if (stored[i] &&
            !memcmp(&buffers->durable_stage[i], &buffers->durable[i], p.width * 2)) {
            continue;
        }
        if (offset + 6 + p.width * 2 > IMAGE_MAX) { return 0; }
        sys_put_le16(p.key.dev_id, buf + offset);
        sys_put_le16(p.key.sig_id, buf + offset + 2);
        buf[offset + 4] = p.type;
        buf[offset + 5] = p.width;
        offset += 6;
        for (size_t w = 0; w < p.width; ++w) {
            sys_put_le16(buffers->durable_stage[i].words[w], buf + offset);
            offset += 2;
        }
        ++count;
    }
    sys_put_le16(count, buf + 6);
    return offset;
}

static int save_parameters(uint32_t sequence)
{
    size_t len = encode_changes(buffers->image, sequence);
    if (!len) { return -E2BIG; }
    int err = dp_store_commit(&fs, buffers->image, len);
    if (!err) {
        memcpy(buffers->durable, buffers->durable_stage, sizeof(buffers->durable));
        for (size_t i = 0; i < param_count; ++i) { stored[i] = true; }
    }
    return err;
}

static int apply(const bool *selected)
{
    bool changed = false, store = false;
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) { continue; }
        struct dp_point_cfg p;
        if (!param(i, &p)) { return -ENOENT; }
        if (!(p.flags & FLAG_PERSISTENCE)) { return -EACCES; }
        int err = dp_check_value(&p, &buffers->staged[i]);
        if (err) { return err; }
        changed |= memcmp(&buffers->cache[i].value, &buffers->staged[i],
                          p.width * 2) != 0;
        buffers->durable_stage[i] = buffers->staged[i];
        store = true;
    }
    if (!changed) { return 0; }
    uint32_t sequence = status.sequence + 1;
    if (store) {
        int err = save_parameters(sequence);
        if (err) {
            set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
            return err;
        }
    }
    commit_cache(sequence, selected);
    return 0;
}

int dp_params_write(struct dp_key key, const struct dp_value *value)
{
    if (!value) { return -EINVAL; }
    int err = ensure_mounted();
    if (err) { return err; }
    k_mutex_lock(&params_lock, K_FOREVER);
    int index = index_of(key);
    if (index < 0) {
        k_mutex_unlock(&params_lock);
        return index;
    }
    struct dp_point_cfg p;
    if (!param(index, &p) || p.source != DP_SOURCE_PARAMETER) {
        k_mutex_unlock(&params_lock);
        return -EACCES;
    }
    buffers->staged[index] = *value;
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    selected[index] = true;
    err = apply(selected);
    k_mutex_unlock(&params_lock);
    return err;
}

int dp_params_read(struct dp_key key, struct dp_point_state *state)
{
    if (!state) { return -EINVAL; }
    int err = ensure_mounted();
    if (err) { return err; }
    int index = index_of(key);
    if (index < 0) { return index; }
    k_mutex_lock(&params_lock, K_FOREVER);
    *state = buffers->cache[index];
    k_mutex_unlock(&params_lock);
    return 0;
}

int dp_params_restore(uint16_t dev_id)
{
    int err = ensure_mounted();
    if (err) { return err; }
    k_mutex_lock(&params_lock, K_FOREVER);
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    bool found = false;
    for (size_t i = 0; i < param_count; ++i) {
        struct dp_point_cfg p;
        if (!param(i, &p) || p.key.dev_id != dev_id ||
            (p.flags & FLAG_NO_RESTORE)) { continue; }
        buffers->staged[i] = p.initial;
        selected[i] = true;
        found = true;
    }
    err = found ? apply(selected) : -ENOENT;
    k_mutex_unlock(&params_lock);
    return err;
}

void dp_params_get_status(struct dp_param_status *out)
{
    if (!out) { return; }
    (void)ensure_mounted();
    k_mutex_lock(&status_lock, K_FOREVER);
    *out = status;
    k_mutex_unlock(&status_lock);
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
    const struct flash_area *area;
    int err = flash_area_open(partition_id, &area);
    if (err) { return err; }
    fs.flash_device = flash_area_get_device(area);
    if (!device_is_ready(fs.flash_device)) { return -ENODEV; }
    err = flash_get_page_info_by_offs(fs.flash_device, area->fa_off, &page);
    if (err) { return err; }
    if (!page.size || page.size > UINT16_MAX || area->fa_off % page.size ||
        area->fa_size % page.size || area->fa_size / page.size < 3) {
        return -EINVAL;
    }
    fs.offset = area->fa_off;
    fs.sector_size = page.size;
    fs.sector_count = area->fa_size / page.size;
    err = nvs_mount(&fs);
    if (err) { return err; }

    for (size_t i = 0; i < param_count; ++i) {
        struct dp_point_cfg p;
        if (!param(i, &p)) { return -ENOENT; }
        buffers->durable_stage[i] = p.initial;
        buffers->staged[i] = p.initial;
    }
    uint32_t sequence = 0;
    bool recovered = false, present = false, converted = false;
    memset(stored, 0, sizeof(stored));
    err = dp_store_open(&fs, buffers->image, sizeof(buffers->image),
                        &sequence, &present, &recovered);
    if (err) { *incompatible = incompatible_format(err); return err; }
    bool seed[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    if (present) {
        for (size_t i = 0; i < param_count; ++i) {
            struct dp_point_cfg p;
            if (!param(i, &p) || !(p.flags & FLAG_PERSISTENCE)) { continue; }
            struct dp_stored_value value;
            err = dp_store_read(&fs, p.key, &value, &recovered);
            if (err == -ENOENT) { seed[i] = true; continue; }
            if (err) { *incompatible = incompatible_format(err); return err; }
            uint8_t bytes[DP_STORE_MAX_WORDS * 2];
            for (size_t w = 0; w < value.width; ++w) {
                sys_put_le16(value.words[w], bytes + w * 2);
            }
            err = load_value(&p, value.type, value.width, bytes,
                             &buffers->durable_stage[i]);
            if (err) { *incompatible = incompatible_format(err); return err; }
            stored[i] = value.width == p.width;
            if (!stored[i]) { converted = true; }
        }
    } else {
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
        struct dp_point_cfg p;
        if (!param(i, &p)) { return -ENOENT; }
        if (seed[i]) {
            err = dp_check_value(&p, &buffers->durable_stage[i]);
            if (err) { return err; }
        }
        need_save |= (p.flags & FLAG_PERSISTENCE) && !stored[i];
    }
    memcpy(buffers->staged, buffers->durable_stage, sizeof(buffers->staged));
    memcpy(buffers->durable, buffers->durable_stage, sizeof(buffers->durable));
    commit_cache(sequence, NULL);
    if (need_save) {
        err = save_parameters(sequence);
        if (err) { return err; }
    }
    set_status(0, DP_PARAM_STORAGE_READY |
               (recovered ? DP_PARAM_HISTORY_RECOVERED : 0) |
               (converted ? DP_PARAM_STORAGE_RESET : 0));
    return 0;
}

static int mount_locked(void)
{
    bool incompatible;
    int err = load_locked(&incompatible);
    if (!incompatible || !fs.ready) { return err; }
    LOG_WRN("Parameter format incompatible (%d): clearing partition %u", err, partition_id);
    err = nvs_clear(&fs);
    if (!err) { err = load_locked(&incompatible); }
    if (!err) { set_status(0, status.flags | DP_PARAM_STORAGE_RESET); }
    return err;
}

/* 由首次登记确定分区；后续登记必须一致。 */
int dp_params_configure(uint8_t area_id)
{
    if (!configured) {
        partition_id = area_id;
        configured = true;
        return 0;
    }
    return partition_id == area_id ? 0 : -EINVAL;
}

bool dp_params_configured(void) { return configured; }

bool dp_params_mounted(void) { return ready; }

/* 调用方持有 params_lock；首次访问时完成缓存分配、参数枚举和 NVS 挂载。 */
static int mount_once(void)
{
    if (ready) {
        return (status.flags & DP_PARAM_STORAGE_READY) ? 0 : -EROFS;
    }
    /* 尚未登记任何模型时不锁存，允许登记后再次访问。 */
    if (!configured) { return -ENODEV; }
    ready = true;
    buffers = k_malloc(sizeof(*buffers));
    if (!buffers) {
        set_status(-ENOMEM, 0);
        return -ENOMEM;
    }
    memset(buffers, 0, sizeof(*buffers));
    for (size_t i = 0; i < dp_point_count(); ++i) {
        struct dp_point_cfg p;
        if (!dp_config_point(i, &p)) { continue; }
        if (p.source != DP_SOURCE_PARAMETER) { continue; }
        if (param_count == CONFIG_DP_PARAMS_MAX_COUNT) {
            set_status(-ENOSPC, 0);
            return -ENOSPC;
        }
        defs[param_count++] = i;
    }
    for (size_t i = 0; i < param_count; ++i) {
        struct dp_point_cfg p;
        if (!param(i, &p)) {
            set_status(-ENOENT, 0);
            return -ENOENT;
        }
        buffers->staged[i] = p.initial;
        buffers->durable_stage[i] = p.initial;
    }
    if (!param_count) {
        set_status(0, DP_PARAM_STORAGE_READY);
        return 0;
    }
    int err = mount_locked();
    if (err) {
        LOG_ERR("Parameter storage initialization failed (%d)", err);
        set_status(err, 0);
    }
    return err;
}

static int ensure_mounted(void)
{
    k_mutex_lock(&params_lock, K_FOREVER);
    int err = mount_once();
    k_mutex_unlock(&params_lock);
    return err;
}
