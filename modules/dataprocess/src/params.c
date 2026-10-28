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
static struct {
    uint16_t device_id;
    const dp_point_def_t *row;
    dp_point_data_t *data;
} defs[CONFIG_DP_PARAMS_MAX_COUNT];
static bool stored[CONFIG_DP_PARAMS_MAX_COUNT];
static struct nvs_fs fs;
/* 应用必须提供固定分区别名；名称约定属于 DP 模块。 */
#define PARAMS_NODE DT_ALIAS(dp_params)
#if !DT_NODE_EXISTS(PARAMS_NODE)
#error "dataprocess requires a dp-params devicetree alias for its parameter partition"
#else
BUILD_ASSERT(DT_NODE_HAS_STATUS(PARAMS_NODE, okay),
             "dp-params partition must be enabled");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_PARENT(PARAMS_NODE), fixed_partitions),
             "dp-params must refer to a fixed partition");
#endif
static const uint8_t partition_id = DT_FIXED_PARTITION_ID(PARAMS_NODE);
static dp_param_status_t status;
static bool ready;
static K_MUTEX_DEFINE(params_lock);
static K_MUTEX_DEFINE(status_lock);

typedef struct {
    dp_value_t staged[CONFIG_DP_PARAMS_MAX_COUNT];
    dp_value_t durable[CONFIG_DP_PARAMS_MAX_COUNT];
    dp_value_t durable_stage[CONFIG_DP_PARAMS_MAX_COUNT];
    uint8_t image[IMAGE_MAX];
} dp_param_buffers_t;

static dp_param_buffers_t *buffers;

static int ensure_mounted(void);

static const dp_point_cfg_t *param(size_t index, dp_point_cfg_t *scratch)
{
    return dp_describe_point(defs[index].device_id, defs[index].row, scratch) ?
           NULL : scratch;
}

static int index_of(dp_key_t key)
{
    for (size_t i = 0; i < param_count; ++i) {
        if (defs[i].device_id == key.device_id && defs[i].row->signal_id == key.signal_id) {
            return i;
        }
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
        defs[i].data->value = buffers->staged[i];
        defs[i].data->updated_at_s = selected ? dp_timestamp_s() : 0;
    }
    status.sequence = sequence;
}

static int load_value(const dp_point_cfg_t *p, uint8_t type, uint8_t width,
                      const uint8_t *bytes, dp_value_t *value)
{
    if (type != p->type || width != p->width) { return -EPROTO; }
    *value = (dp_value_t){0};
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
        dp_point_cfg_t p;
        if (!param(i, &p) || (p.flags & FLAG_PERSISTENCE) == 0) { continue; }
        if (stored[i] &&
            !memcmp(&buffers->durable_stage[i], &buffers->durable[i], p.width * 2)) {
            continue;
        }
        if (offset + 6 + p.width * 2 > IMAGE_MAX) { return 0; }
        sys_put_le16(p.key.device_id, buf + offset);
        sys_put_le16(p.key.signal_id, buf + offset + 2);
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
        dp_point_cfg_t p;
        if (!param(i, &p)) { return -ENOENT; }
        if (!(p.flags & FLAG_PERSISTENCE)) { return -EACCES; }
        int err = dp_check_value(&p, &buffers->staged[i]);
        if (err) { return err; }
        changed |= memcmp(&defs[i].data->value, &buffers->staged[i],
                          p.width * 2) != 0;
        buffers->durable_stage[i] = buffers->staged[i];
        store = true;
    }
    if (!changed) {
        for (size_t i = 0; i < param_count; ++i) {
            if (selected[i]) { defs[i].data->updated_at_s = dp_timestamp_s(); }
        }
        return 0;
    }
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

int dp_params_write(dp_key_t key, const dp_value_t *value)
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
    dp_point_cfg_t p;
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

int dp_params_read(dp_key_t key, dp_point_data_t *state)
{
    if (!state) { return -EINVAL; }
    int err = ensure_mounted();
    if (err) { return err; }
    int index = index_of(key);
    if (index < 0) { return index; }
    k_mutex_lock(&params_lock, K_FOREVER);
    *state = *defs[index].data;
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
        dp_point_cfg_t p;
        if (!param(i, &p) || p.key.device_id != dev_id ||
            (p.flags & FLAG_NO_RESTORE)) { continue; }
        buffers->staged[i] = p.initial;
        selected[i] = true;
        found = true;
    }
    err = found ? apply(selected) : -ENOENT;
    k_mutex_unlock(&params_lock);
    return err;
}

void dp_params_get_status(dp_param_status_t *out)
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
        dp_point_cfg_t p;
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
            dp_point_cfg_t p;
            if (!param(i, &p) || !(p.flags & FLAG_PERSISTENCE)) { continue; }
            dp_stored_value_t value;
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
        dp_point_cfg_t p;
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

bool dp_params_mounted(void) { return ready; }

/* 调用方持有 params_lock；首次访问时完成缓存分配、参数枚举和 NVS 挂载。 */
static int mount_once(void)
{
    if (ready) {
        return (status.flags & DP_PARAM_STORAGE_READY) ? 0 : -EROFS;
    }
    /* 尚未登记任何点表时不锁存，允许登记后再次访问。 */
    if (!dp_tables()) { return -ENODEV; }
    ready = true;
    buffers = k_malloc(sizeof(*buffers));
    if (!buffers) {
        set_status(-ENOMEM, 0);
        return -ENOMEM;
    }
    memset(buffers, 0, sizeof(*buffers));
    for (dp_registered_table_t *table = dp_tables(); table; table = table->next) {
        for (size_t i = 0; i < table->point_count; ++i) {
            const dp_point_def_t *row = &table->points[i];
            if (!(row->flags & FLAG_PERSISTENCE)) { continue; }
            defs[param_count].device_id = table->device_id;
            defs[param_count].data = &table->data[i];
            defs[param_count++].row = row;
        }
    }
    for (size_t i = 0; i < param_count; ++i) {
        dp_point_cfg_t p;
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
