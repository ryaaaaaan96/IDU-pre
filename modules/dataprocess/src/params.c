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
    k_mutex_lock(&status_lock, K_FOREVER);
    status.sequence = sequence;
    k_mutex_unlock(&status_lock);
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
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        k_mutex_unlock(&params_lock);
        return ready ? -EROFS : -EACCES;
    }
    int err;
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
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        k_mutex_unlock(&params_lock);
        return ready ? -EROFS : -EACCES;
    }
    int index = index_of(device_id, signal_key);
    if (index < 0) {
        k_mutex_unlock(&params_lock);
        return index;
    }
    *state = *defs[index].data;
    k_mutex_unlock(&params_lock);
    return 0;
}

int dp_params_restore(uint16_t dev_id)
{
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        k_mutex_unlock(&params_lock);
        return ready ? -EROFS : -EACCES;
    }
    int err;
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
    k_mutex_lock(&status_lock, K_FOREVER);
    *out = status;
    k_mutex_unlock(&status_lock);
}

static bool incompatible_format(int err)
{
    return err == -EPROTO || err == -EMSGSIZE || err == -EEXIST;
}

static int mount_store(bool *incompatible)
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

    status.sequence = sequence;
    set_status(0, DP_PARAM_STORAGE_READY | (recovered ? DP_PARAM_HISTORY_RECOVERED : 0));
    return 0;
}

int dp_init(void)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&params_lock, K_FOREVER);
    if (ready) {
        int err = (status.flags & DP_PARAM_STORAGE_READY) ? 0 : -EROFS;
        k_mutex_unlock(&params_lock);
        return err;
    }
    buffers = k_calloc(1, sizeof(*buffers));
    if (!buffers) {
        k_mutex_unlock(&params_lock);
        return -ENOMEM;
    }
    bool incompatible;
    int err = mount_store(&incompatible);
    if (incompatible && fs.ready) {
        LOG_WRN("Parameter format incompatible (%d): clearing partition %u", err, partition_id);
        err = nvs_clear(&fs);
        if (!err) {
            err = mount_store(&incompatible);
        }
        if (!err) {
            set_status(0, status.flags | DP_PARAM_STORAGE_RESET);
        }
    }
    if (err) {
        set_status(err, 0);
        k_free(buffers);
        buffers = NULL;
    } else {
        ready = true;
    }
    k_mutex_unlock(&params_lock);
    return err;
}

/* 启动线程串行登记；节点发布前加载本表，缺失记录一次提交。 */
int dp_params_register(dp_registered_table_t *table)
{
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        k_mutex_unlock(&params_lock);
        return ready ? -EROFS : -EACCES;
    }
    size_t first = param_count;
    bool need_save = false, recovered = false;
    int err = 0;
    for (size_t i = 0; i < table->point_count; ++i) {
        const dp_point_config_t *point = &table->points[i];
        err = dp_validate_point(table->device_id, point);
        if (err) {
            err = -EINVAL;
            break;
        }
        value_t initial = point->data.initial;
        err = dp_check_value(point, &initial);
        if (err) {
            err = -EINVAL;
            break;
        }
        table->data[i].value = initial;
        if (!(point->flags & DP_POINT_FLAG_PERSISTENT)) {
            continue;
        }
        if (param_count == CONFIG_DP_PARAMS_MAX_COUNT) {
            err = -ENOSPC;
            break;
        }
        value_t value = initial;
        dp_stored_value_t history;
        err = dp_store_read(&fs, table->device_id, point->signal_key, &history, &recovered);
        bool found = !err;
        if (err == -ENOENT) {
            err = 0;
        } else if (!err) {
            if (history.type != storage_type(point) || history.width != storage_width(point)) {
                err = -EPROTO;
                break;
            }
            value = (value_t){0};
            memcpy(value.words, history.words, history.width * sizeof(history.words[0]));
        }
        if (err) {
            break;
        }
        err = dp_check_value(point, &value);
        if (err) {
            break;
        }
        defs[param_count].device_id = table->device_id;
        defs[param_count].row = point;
        defs[param_count].data = &table->data[i];
        table->data[i].value = value;
        buffers->staged[param_count] = value;
        buffers->durable_stage[param_count] = value;
        buffers->durable[param_count] = value;
        stored[param_count++] = found;
        need_save |= !found;
    }
    if (!err && need_save) {
        err = save_parameters(status.sequence + 1);
        if (err) {
            /* 提交可能已部分落盘，冻结后续写入，重启时恢复提交日志。 */
            set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
        } else {
            k_mutex_lock(&status_lock, K_FOREVER);
            ++status.sequence;
            k_mutex_unlock(&status_lock);
        }
    }
    if (err) {
        memset(&defs[first], 0, (param_count - first) * sizeof(defs[0]));
        param_count = first;
    }
    if (recovered) {
        set_status(status.error, status.flags | DP_PARAM_HISTORY_RECOVERED);
    }
    k_mutex_unlock(&params_lock);
    return err;
}
