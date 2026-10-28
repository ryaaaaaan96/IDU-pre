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
    const dp_point_config_t *row;
    dp_point_data_t *data;
} defs[CONFIG_DP_PARAMS_MAX_COUNT];

static bool stored[CONFIG_DP_PARAMS_MAX_COUNT];
static struct nvs_fs fs;
/* 应用必须提供固定分区别名；名称约定属于 DP 模块。 */
#define PARAMS_NODE DT_ALIAS(dp_params)
#if !DT_NODE_EXISTS(PARAMS_NODE)
#error "dataprocess requires a dp-params devicetree alias for its parameter partition"
#else
BUILD_ASSERT(DT_NODE_HAS_STATUS(PARAMS_NODE, okay), "dp-params partition must be enabled");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_PARENT(PARAMS_NODE), fixed_partitions),
    "dp-params must refer to a fixed partition");
#endif
static const uint8_t partition_id = DT_FIXED_PARTITION_ID(PARAMS_NODE);
static dp_param_status_t status;
static bool ready;
static K_MUTEX_DEFINE(params_lock);
static K_MUTEX_DEFINE(status_lock);

typedef struct {
    value_t staged[CONFIG_DP_PARAMS_MAX_COUNT];
    value_t durable[CONFIG_DP_PARAMS_MAX_COUNT];
    value_t durable_stage[CONFIG_DP_PARAMS_MAX_COUNT];
    uint8_t image[IMAGE_MAX];
} dp_param_buffers_t;

static dp_param_buffers_t *buffers;

static int ensure_mounted(void);

/* NVS 编码固定，独立于公共 value_type_t 的枚举顺序。 */
static dp_storage_type_t storage_type(const dp_point_config_t *point)
{
    switch (point->type) {
    case VALUE_TYPE_U8:
        return DP_U8;
    case VALUE_TYPE_U16:
        return DP_U16;
    case VALUE_TYPE_S16:
        return DP_S16;
    case VALUE_TYPE_U32:
        return DP_U32;
    case VALUE_TYPE_S32:
        return DP_S32;
    default:
        return DP_WORDS;
    }
}

static uint8_t storage_width(const dp_point_config_t *point)
{
    return (point->size + 1) / 2;
}

static int index_of(uint16_t device_id, uint16_t signal_key)
{
    for (size_t i = 0; i < param_count; ++i) {
        if (defs[i].device_id == device_id && defs[i].row->signal_key == signal_key) {
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
    if (flags & DP_PARAM_STORAGE_READY) {
        status.storage_error = 0;
    } else if (error) {
        status.storage_error = error;
    }
    k_mutex_unlock(&status_lock);
}

static void commit_cache(uint32_t sequence, const bool *selected)
{
    for (size_t i = 0; i < param_count; ++i) {
        if (selected && !selected[i]) {
            continue;
        }
        defs[i].data->value = buffers->staged[i];
        defs[i].data->updated_at_s = selected ? dp_timestamp_s() : 0;
    }
    status.sequence = sequence;
}

/* 调用方已挂载 NVS 并恢复提交镜像；只在缺少记录时使用配置初值。 */
static int dp_load_value(uint16_t device_id,
    const dp_point_config_t *point,
    bool history_present,
    value_t *value,
    bool *restored,
    bool *recovered)
{
    *restored = false;
    *value = point->data.initial;
    if (!(point->flags & DP_POINT_FLAG_PERSISTENT) || !history_present) {
        return dp_check_value(point, value);
    }

    dp_stored_value_t history;
    int err = dp_store_read(&fs, device_id, point->signal_key, &history, recovered);
    if (err == -ENOENT) {
        return dp_check_value(point, value);
    }
    if (err) {
        return err;
    }
    if (history.type != storage_type(point) || history.width != storage_width(point)) {
        return -EPROTO;
    }
    *value = (value_t){0};
    memcpy(value->words, history.words, history.width * sizeof(history.words[0]));
    err = dp_check_value(point, value);
    if (!err) {
        *restored = true;
    }
    return err;
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
        const dp_point_config_t *p = defs[i].row;
        if (stored[i] &&
            !memcmp(&buffers->durable_stage[i], &buffers->durable[i], storage_width(p) * 2)) {
            continue;
        }
        if (offset + 6 + storage_width(p) * 2 > IMAGE_MAX) {
            return 0;
        }
        sys_put_le16(defs[i].device_id, buf + offset);
        sys_put_le16(p->signal_key, buf + offset + 2);
        buf[offset + 4] = storage_type(p);
        buf[offset + 5] = storage_width(p);
        offset += 6;
        for (size_t w = 0; w < storage_width(p); ++w) {
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
    if (!len) {
        return -E2BIG;
    }
    int err = dp_store_commit(&fs, buffers->image, len);
    if (!err) {
        memcpy(buffers->durable, buffers->durable_stage, sizeof(buffers->durable));
        for (size_t i = 0; i < param_count; ++i) {
            stored[i] = true;
        }
    }
    return err;
}

static int apply(const bool *selected)
{
    bool changed = false, store = false;
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const dp_point_config_t *p = defs[i].row;
        if (!(p->flags & DP_POINT_FLAG_PERSISTENT)) {
            return -EACCES;
        }
        int err = dp_check_value(p, &buffers->staged[i]);
        if (err) {
            return err;
        }
        changed |= memcmp(&defs[i].data->value, &buffers->staged[i], storage_width(p) * 2) != 0;
        buffers->durable_stage[i] = buffers->staged[i];
        store = true;
    }
    if (!changed) {
        for (size_t i = 0; i < param_count; ++i) {
            if (selected[i]) {
                defs[i].data->updated_at_s = dp_timestamp_s();
            }
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

int dp_params_write(uint16_t device_id, uint16_t signal_key, const value_t *value)
{
    if (!value) {
        return -EINVAL;
    }
    int err = ensure_mounted();
    if (err) {
        return err;
    }
    k_mutex_lock(&params_lock, K_FOREVER);
    int index = index_of(device_id, signal_key);
    if (index < 0) {
        k_mutex_unlock(&params_lock);
        return index;
    }
    buffers->staged[index] = *value;
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    selected[index] = true;
    err = apply(selected);
    k_mutex_unlock(&params_lock);
    return err;
}

int dp_params_read(uint16_t device_id, uint16_t signal_key, dp_point_data_t *state)
{
    if (!state) {
        return -EINVAL;
    }
    int err = ensure_mounted();
    if (err) {
        return err;
    }
    int index = index_of(device_id, signal_key);
    if (index < 0) {
        return index;
    }
    k_mutex_lock(&params_lock, K_FOREVER);
    *state = *defs[index].data;
    k_mutex_unlock(&params_lock);
    return 0;
}

int dp_params_restore(uint16_t dev_id)
{
    int err = ensure_mounted();
    if (err) {
        return err;
    }
    k_mutex_lock(&params_lock, K_FOREVER);
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    bool found = false;
    for (size_t i = 0; i < param_count; ++i) {
        const dp_point_config_t *p = defs[i].row;
        if (defs[i].device_id != dev_id || (p->flags & DP_POINT_FLAG_SKIP_RESTORE)) {
            continue;
        }
        buffers->staged[i] = p->data.initial;
        selected[i] = true;
        found = true;
    }
    err = found ? apply(selected) : -ENOENT;
    k_mutex_unlock(&params_lock);
    return err;
}

void dp_params_get_status(dp_param_status_t *out)
{
    if (!out) {
        return;
    }
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
        area->fa_size % page.size || area->fa_size / page.size < 3) {
        return -EINVAL;
    }
    fs.offset = area->fa_off;
    fs.sector_size = page.size;
    fs.sector_count = area->fa_size / page.size;
    err = nvs_mount(&fs);
    if (err) {
        return err;
    }

    uint32_t sequence = 0;
    bool recovered = false, present = false;
    memset(stored, 0, sizeof(stored));
    err =
        dp_store_open(&fs, buffers->image, sizeof(buffers->image), &sequence, &present, &recovered);
    if (err) {
        *incompatible = incompatible_format(err);
        return err;
    }
    if (!present) {
        uint8_t probe;
        ssize_t legacy = nvs_read(&fs, 1, &probe, sizeof(probe));
        if (legacy != -ENOENT) {
            *incompatible = legacy >= 0;
            return legacy < 0 ? (int)legacy : -EPROTO;
        }
    }

    bool need_save = !present;
    for (size_t i = 0; i < param_count; ++i) {
        err = dp_load_value(defs[i].device_id,
            defs[i].row,
            present,
            &buffers->durable_stage[i],
            &stored[i],
            &recovered);
        if (err) {
            *incompatible = incompatible_format(err);
            return err;
        }
        need_save |= !stored[i];
    }
    memcpy(buffers->staged, buffers->durable_stage, sizeof(buffers->staged));
    memcpy(buffers->durable, buffers->durable_stage, sizeof(buffers->durable));
    commit_cache(sequence, NULL);
    if (need_save) {
        err = save_parameters(sequence);
        if (err) {
            return err;
        }
    }
    set_status(0, DP_PARAM_STORAGE_READY | (recovered ? DP_PARAM_HISTORY_RECOVERED : 0));
    return 0;
}

static int mount_locked(void)
{
    bool incompatible;
    int err = load_locked(&incompatible);
    if (!incompatible || !fs.ready) {
        return err;
    }
    LOG_WRN("Parameter format incompatible (%d): clearing partition %u", err, partition_id);
    err = nvs_clear(&fs);
    if (!err) {
        err = load_locked(&incompatible);
    }
    if (!err) {
        set_status(0, status.flags | DP_PARAM_STORAGE_RESET);
    }
    return err;
}

bool dp_params_mounted(void)
{
    return ready;
}

/* 调用方持有 params_lock；首次访问时完成缓存分配、参数枚举和 NVS 挂载。 */
static int mount_once(void)
{
    if (ready) {
        return (status.flags & DP_PARAM_STORAGE_READY) ? 0 : -EROFS;
    }
    /* 尚未登记任何点表时不锁存，允许登记后再次访问。 */
    if (!dp_tables()) {
        return -ENODEV;
    }
    ready = true;
    buffers = k_malloc(sizeof(*buffers));
    if (!buffers) {
        set_status(-ENOMEM, 0);
        return -ENOMEM;
    }
    memset(buffers, 0, sizeof(*buffers));
    for (dp_registered_table_t *table = dp_tables(); table; table = table->next) {
        for (size_t i = 0; i < table->point_count; ++i) {
            const dp_point_config_t *row = &table->points[i];
            if (!(row->flags & DP_POINT_FLAG_PERSISTENT)) {
                continue;
            }
            defs[param_count].device_id = table->device_id;
            defs[param_count].data = &table->data[i];
            defs[param_count++].row = row;
        }
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
