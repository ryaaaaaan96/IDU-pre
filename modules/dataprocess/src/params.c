#include <dataprocess/params.h>
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(dp_params, LOG_LEVEL_INF);

/* 编码属于 NVS 格式，不能随公共类型枚举重排。 */
typedef enum {
    DP_U16 = 0,
    DP_S16 = 1,
    DP_U32 = 2,
    DP_S32 = 3,
    DP_WORDS = 4,
    DP_U8 = 5,
} dp_storage_type_t;

/* ID 2 保留；普通参数不使用点表下标作为存储 ID。 */
#define DP_STORE_META_ID 3U
#define DP_STORE_COMMIT_ID 4U
#define DP_STORE_IMAGE_MAGIC 0x50554449U
#define DP_STORE_VERSION 2U
#define DP_STORE_IMAGE_HEADER 16U
#define DP_STORE_VALUE_HEADER 16U
#define DP_STORE_MAX_WORDS 72U

typedef struct {
    uint8_t type, width;
    uint16_t words[DP_STORE_MAX_WORDS];
} dp_stored_value_t;

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
/* 运行期统一保护参数缓存、工作区、NVS 事务及状态；启动初始化/登记独占访问。 */
static K_MUTEX_DEFINE(params_lock);

typedef struct {
    value_t staged[CONFIG_DP_PARAMS_MAX_COUNT];
    value_t durable[CONFIG_DP_PARAMS_MAX_COUNT];
    value_t durable_stage[CONFIG_DP_PARAMS_MAX_COUNT];
    uint8_t image[IMAGE_MAX];
} dp_param_buffers_t;

static dp_param_buffers_t *buffers;

/* ==================== NVS 编码与事务恢复 ==================== */
#define VALUE_MAGIC 0x56554449U
#define META_MAGIC 0x4d554449U
#define SLOT_FIRST 0x100U
#define SLOT_COUNT 1024U
#define VALUE_MAX (DP_STORE_VALUE_HEADER + 2U * DP_STORE_MAX_WORDS)

static bool valid_value(uint8_t type, uint8_t width)
{
    if (type > DP_U8 || !width || width > DP_STORE_MAX_WORDS) {
        return false;
    }
    return type == DP_WORDS || width == ((type == DP_U32 || type == DP_S32) ? 2 : 1);
}

static int read_history(uint16_t id, uint8_t *data, size_t capacity, bool *recovered)
{
    for (uint16_t i = 0; i < 8; ++i) {
        ssize_t len = nvs_read_hist(&fs, id, data, capacity, i);
        if (len == -EIO) {
            *recovered = true;
            continue;
        }
        if (len >= 0 && (size_t)len > capacity) {
            return -EMSGSIZE;
        }
        /* 最新条目损坏不能当作键不存在或新键处理。 */
        return len == -ENOENT && i ? -EIO : (int)len;
    }
    return -EIO;
}

static int find_slot(
    uint16_t device_id, uint16_t signal_key, uint16_t *id, uint8_t *record, bool *recovered)
{
    uint8_t bytes[4];
    sys_put_le16(device_id, bytes);
    sys_put_le16(signal_key, bytes + 2);
    uint16_t start = crc32_ieee(bytes, sizeof(bytes)) % SLOT_COUNT;
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        *id = SLOT_FIRST + (start + i) % SLOT_COUNT;
        int len = read_history(*id, record, VALUE_MAX, recovered);
        if (len == -ENOENT) {
            return len;
        }
        if (len < 0) {
            return len;
        }
        if (len < DP_STORE_VALUE_HEADER || sys_get_le32(record) != VALUE_MAGIC ||
            sys_get_le16(record + 4) != DP_STORE_VERSION || !sys_get_le16(record + 6) ||
            !sys_get_le16(record + 8) || !valid_value(record[10], record[11]) ||
            len != DP_STORE_VALUE_HEADER + 2U * record[11]) {
            return -EPROTO;
        }
        if (sys_get_le16(record + 6) == device_id && sys_get_le16(record + 8) == signal_key) {
            return len;
        }
    }
    return -ENOSPC;
}

static int read_stored_value(
    uint16_t device_id, uint16_t signal_key, dp_stored_value_t *value, bool *recovered)
{
    uint8_t record[VALUE_MAX];
    uint16_t id;
    int len = find_slot(device_id, signal_key, &id, record, recovered);
    if (len < 0) {
        return len;
    }
    *value = (dp_stored_value_t){.type = record[10], .width = record[11]};
    for (size_t w = 0; w < value->width; ++w) {
        value->words[w] = sys_get_le16(record + DP_STORE_VALUE_HEADER + w * 2);
    }
    return 0;
}

static int validate_image(const uint8_t *image, size_t length)
{
    if (length < DP_STORE_IMAGE_HEADER || sys_get_le32(image) != DP_STORE_IMAGE_MAGIC ||
        sys_get_le16(image + 4) != DP_STORE_VERSION || sys_get_le32(image + 8)) {
        return -EPROTO;
    }
    size_t offset = DP_STORE_IMAGE_HEADER;
    for (size_t i = 0; i < sys_get_le16(image + 6); ++i) {
        if (offset + 6 > length || !sys_get_le16(image + offset) ||
            !sys_get_le16(image + offset + 2) ||
            !valid_value(image[offset + 4], image[offset + 5]) ||
            offset + 6 + 2U * image[offset + 5] > length) {
            return -EPROTO;
        }
        for (size_t at = DP_STORE_IMAGE_HEADER; at < offset; at += 6 + 2U * image[at + 5]) {
            if (sys_get_le16(image + at) == sys_get_le16(image + offset) &&
                sys_get_le16(image + at + 2) == sys_get_le16(image + offset + 2)) {
                return -EEXIST;
            }
        }
        offset += 6 + 2U * image[offset + 5];
    }
    return offset == length ? 0 : -EPROTO;
}

static int apply_image(const uint8_t *image, size_t length)
{
    int err = validate_image(image, length);
    if (err) {
        return err;
    }
    uint8_t record[VALUE_MAX];
    uint32_t sequence = sys_get_le32(image + 12);
    for (size_t at = DP_STORE_IMAGE_HEADER; at < length; at += 6 + 2U * image[at + 5]) {
        uint16_t device_id = sys_get_le16(image + at);
        uint16_t signal_key = sys_get_le16(image + at + 2);
        uint16_t id;
        bool recovered = false;
        err = find_slot(device_id, signal_key, &id, record, &recovered);
        if (err < 0 && err != -ENOENT) {
            return err;
        }
        sys_put_le32(VALUE_MAGIC, record);
        sys_put_le16(DP_STORE_VERSION, record + 4);
        sys_put_le16(device_id, record + 6);
        sys_put_le16(signal_key, record + 8);
        record[10] = image[at + 4];
        record[11] = image[at + 5];
        sys_put_le32(sequence, record + 12);
        memcpy(record + DP_STORE_VALUE_HEADER, image + at + 6, 2U * record[11]);
        ssize_t rc = nvs_write(&fs, id, record, DP_STORE_VALUE_HEADER + 2U * record[11]);
        if (rc < 0) {
            return rc;
        }
    }
    uint8_t meta[12] = {0};
    sys_put_le32(META_MAGIC, meta);
    sys_put_le16(DP_STORE_VERSION, meta + 4);
    sys_put_le32(sequence, meta + 8);
    ssize_t rc = nvs_write(&fs, DP_STORE_META_ID, meta, sizeof(meta));
    if (rc < 0) {
        return rc;
    }
    return nvs_delete(&fs, DP_STORE_COMMIT_ID);
}

static int commit_image(const uint8_t *image, size_t length)
{
    int err = validate_image(image, length);
    if (err) {
        return err;
    }
    uint8_t probe;
    ssize_t rc = nvs_read(&fs, DP_STORE_COMMIT_ID, &probe, sizeof(probe));
    if (rc != -ENOENT) {
        return rc < 0 ? (int)rc : -EBUSY;
    }
    /* 数值未变化时，重复刷新不应磨损 Flash。 */
    if (!sys_get_le16(image + 6)) {
        uint8_t meta[12];
        rc = nvs_read(&fs, DP_STORE_META_ID, meta, sizeof(meta));
        if (rc == sizeof(meta) && sys_get_le32(meta) == META_MAGIC &&
            sys_get_le16(meta + 4) == DP_STORE_VERSION && !sys_get_le16(meta + 6) &&
            sys_get_le32(meta + 8) == sys_get_le32(image + 12)) {
            return 0;
        }
        if (rc < 0 && rc != -ENOENT) {
            return rc;
        }
    }
    rc = nvs_write(&fs, DP_STORE_COMMIT_ID, image, length);
    return rc < 0 ? (int)rc : apply_image(image, length);
}

static int open_store(
    uint8_t *scratch, size_t capacity, uint32_t *sequence, bool *present, bool *recovered)
{
    ssize_t len = nvs_read(&fs, DP_STORE_COMMIT_ID, scratch, capacity);
    if (len != -ENOENT) {
        if (len < 0 || len > capacity) {
            return len < 0 ? (int)len : -EMSGSIZE;
        }
        int err = apply_image(scratch, len);
        if (err) {
            return err;
        }
        *recovered = true;
    }
    uint8_t meta[12];
    len = nvs_read(&fs, DP_STORE_META_ID, meta, sizeof(meta));
    if (len == -ENOENT) {
        *present = false;
        *sequence = 0;
        return 0;
    }
    if (len < 0) {
        return len;
    }
    if (len != sizeof(meta) || sys_get_le32(meta) != META_MAGIC ||
        sys_get_le16(meta + 4) != DP_STORE_VERSION || sys_get_le16(meta + 6)) {
        return -EPROTO;
    }
    *present = true;
    *sequence = sys_get_le32(meta + 8);
    return 0;
}

/* ==================== 参数缓存与运行接口 ==================== */

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

/* 调用方持有 params_lock，或处于无并发的启动初始化/登记阶段。 */
static void set_status(int error, uint16_t flags)
{
    status.error = error;
    status.flags = flags;
    if (flags & DP_PARAM_STORAGE_READY) {
        status.storage_error = 0;
    } else if (error) {
        status.storage_error = error;
    }
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
    int err = commit_image(buffers->image, len);
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
    bool changed = false;
    for (size_t i = 0; i < param_count; ++i) {
        if (!selected[i]) {
            continue;
        }
        const dp_point_config_t *p = defs[i].row;
        int err = dp_check_value(p, &buffers->staged[i]);
        if (err) {
            return err;
        }
        changed |= memcmp(&defs[i].data->value, &buffers->staged[i], storage_width(p) * 2) != 0;
        buffers->durable_stage[i] = buffers->staged[i];
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
    int err = save_parameters(sequence);
    if (err) {
        set_status(err, status.flags & ~DP_PARAM_STORAGE_READY);
        return err;
    }
    for (size_t i = 0; i < param_count; ++i) {
        if (selected[i]) {
            defs[i].data->value = buffers->staged[i];
            defs[i].data->updated_at_s = dp_timestamp_s();
        }
    }
    status.sequence = sequence;
    return 0;
}

int dp_params_write(uint16_t device_id, uint16_t signal_key, const value_t *value)
{
    if (!value) {
        return -EINVAL;
    }
    int err;
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        err = ready ? -EROFS : -EACCES;
        goto unlock;
    }
    int index = index_of(device_id, signal_key);
    if (index < 0) {
        err = index;
        goto unlock;
    }
    buffers->staged[index] = *value;
    bool selected[CONFIG_DP_PARAMS_MAX_COUNT] = {0};
    selected[index] = true;
    err = apply(selected);
unlock:
    k_mutex_unlock(&params_lock);
    return err;
}

int dp_params_read(uint16_t device_id, uint16_t signal_key, dp_point_data_t *state)
{
    if (!state) {
        return -EINVAL;
    }
    int err;
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        err = ready ? -EROFS : -EACCES;
        goto unlock;
    }
    int index = index_of(device_id, signal_key);
    if (index < 0) {
        err = index;
        goto unlock;
    }
    *state = *defs[index].data;
    err = 0;
unlock:
    k_mutex_unlock(&params_lock);
    return err;
}

int dp_params_restore(uint16_t dev_id)
{
    int err;
    k_mutex_lock(&params_lock, K_FOREVER);
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
        err = ready ? -EROFS : -EACCES;
        goto unlock;
    }
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
unlock:
    k_mutex_unlock(&params_lock);
    return err;
}

void dp_params_get_status(dp_param_status_t *out)
{
    if (!out) {
        return;
    }
    k_mutex_lock(&params_lock, K_FOREVER);
    *out = status;
    k_mutex_unlock(&params_lock);
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
    err = open_store(buffers->image, sizeof(buffers->image), &sequence, &present, &recovered);
    if (err) {
        *incompatible = err == -EPROTO || err == -EMSGSIZE || err == -EEXIST;
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
    if (ready) {
        int err = (status.flags & DP_PARAM_STORAGE_READY) ? 0 : -EROFS;
        return err;
    }
    buffers = k_calloc(1, sizeof(*buffers));
    if (!buffers) {
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
    return err;
}

/* 启动线程串行登记；节点发布前加载本表，缺失记录一次提交。 */
int dp_params_register(dp_registered_table_t *table)
{
    if (!(status.flags & DP_PARAM_STORAGE_READY)) {
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
        err = read_stored_value(table->device_id, point->signal_key, &history, &recovered);
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
            ++status.sequence;
        }
    }
    if (err) {
        memset(&defs[first], 0, (param_count - first) * sizeof(defs[0]));
        param_count = first;
    }
    if (recovered) {
        set_status(status.error, status.flags | DP_PARAM_HISTORY_RECOVERED);
    }
    return err;
}
