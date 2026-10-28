#include <dataprocess/points.h>
#include "dp_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp, LOG_LEVEL_INF);

/* 单次分配中不会使节点头及点数据总长度溢出的最大点数。 */
#define DP_MAX_ALLOCATABLE_POINTS                                                                  \
    ((SIZE_MAX - sizeof(dp_registered_table_t)) / sizeof(dp_point_data_t))

/* 应用只提供只读点表，设备目录与点数据均由模块私有持有。 */
static dp_registered_table_t *tables;
static K_MUTEX_DEFINE(store_lock);

static int64_t number(value_type_t type, const value_t *value)
{
    switch (type) {
    case VALUE_TYPE_U8:
        return value->u8;
    case VALUE_TYPE_S16:
        return value->s16;
    case VALUE_TYPE_S32:
        return value->s32;
    case VALUE_TYPE_U32:
        return value->u32;
    default:
        return value->u16;
    }
}

static int check_value(const dp_point_config_t *point, const value_t *value)
{
    if (!point || !value) {
        return -EINVAL;
    }
    if (point->type == VALUE_TYPE_U8 && value->u16 > UINT8_MAX) {
        return -ERANGE;
    }
    if (point->type == VALUE_TYPE_STRING || point->type == VALUE_TYPE_BYTES ||
        point->type == VALUE_TYPE_WORDS) {
        if ((point->size & 1U) && (value->words[point->size / 2] & 0xff00U)) {
            return -ERANGE;
        }
        return 0;
    }
    if (!(point->flags & DP_POINT_FLAG_CHECK_RANGE)) {
        return 0;
    }
    int64_t n = number(point->type, value);
    return n < number(point->type, &point->data.minimum) ||
                   n > number(point->type, &point->data.maximum)
               ? -ERANGE
               : 0;
}

static void unpack(value_t *value, const uint8_t *bytes, size_t size)
{
    *value = (value_t){0};
    for (size_t i = 0; i < size; ++i) {
        value->words[i / 2] |= (uint16_t)bytes[i] << (8 * (i % 2));
    }
}

static void pack(uint8_t *bytes, const value_t *value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = (uint8_t)(value->words[i / 2] >> (8 * (i % 2)));
    }
}

/* 登记时校验一次；运行期间配置常驻只读，不再解析或复制配置行。 */
static int validate_point(uint16_t device_id, const dp_point_config_t *point)
{
    if (!point->signal_key || !point->size || point->size > DP_POINT_WORDS * 2) {
        return -EINVAL;
    }
    switch (point->type) {
    case VALUE_TYPE_U8:
        if (point->size != 1) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
        if (point->size != 2) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        if (point->size != 4) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_WORDS:
        if (point->size & 1U) {
            return -EINVAL;
        }
        __fallthrough;
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_BYTES:
        if (point->flags & DP_POINT_FLAG_CHECK_RANGE) {
            LOG_WRN("Device %u key %u: range check ignored for nonnumeric point",
                device_id,
                point->signal_key);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

dp_registered_table_t *dp_tables(void)
{
    return tables;
}

static int find_point(uint16_t device_id,
    uint16_t signal_key,
    const dp_point_config_t **point,
    dp_point_data_t **state,
    size_t *signal_index)
{
    if (!device_id || !signal_key) {
        return -EINVAL;
    }
    for (dp_registered_table_t *table = tables; table; table = table->next) {
        if (table->device_id != device_id) {
            continue;
        }
        for (size_t i = 0; i < table->point_count; ++i) {
            if (table->points[i].signal_key != signal_key) {
                continue;
            }
            *signal_index = i;
            *state = &table->data[i];
            *point = &table->points[i];
            return 0;
        }
        return -ENOENT;
    }
    return -ENOENT;
}

static int compare_keys(const void *left, const void *right)
{
    uint16_t a = *(const uint16_t *)left;
    uint16_t b = *(const uint16_t *)right;
    return (a > b) - (a < b);
}

/* 只排序临时键副本，保留原表行顺序及回调索引。 */
static int check_unique_keys(const dp_table_t *table)
{
    if (table->point_count < 2) {
        return 0;
    }
    uint16_t *keys = k_malloc(table->point_count * sizeof(*keys));
    if (!keys) {
        return -ENOMEM;
    }
    for (size_t i = 0; i < table->point_count; ++i) {
        keys[i] = table->points[i].signal_key;
    }
    qsort(keys, table->point_count, sizeof(*keys), compare_keys);
    int err = 0;
    for (size_t i = 1; i < table->point_count; ++i) {
        if (keys[i - 1] == keys[i]) {
            err = -EEXIST;
            break;
        }
    }
    k_free(keys);
    return err;
}

int dp_register_table(uint16_t device_id, const dp_table_t *table)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!device_id || !table || !table->points || !table->point_count) {
        return -EINVAL;
    }
    for (const dp_registered_table_t *item = tables; item; item = item->next) {
        if (item->device_id == device_id) {
            return -EEXIST;
        }
    }
    if (table->point_count > DP_MAX_ALLOCATABLE_POINTS) {
        return -EOVERFLOW;
    }

    int err = check_unique_keys(table);
    if (err) {
        return err;
    }

    dp_registered_table_t *entry =
        k_calloc(1, sizeof(*entry) + table->point_count * sizeof(entry->data[0]));
    if (!entry) {
        return -ENOMEM;
    }
    entry->device_id = device_id;
    entry->points = table->points;
    entry->point_count = table->point_count;
    for (size_t i = 0; i < table->point_count; ++i) {
        const dp_point_config_t *point = &table->points[i];
        err = validate_point(device_id, point);
        if (!err) {
            entry->data[i].value = point->data.initial;
            err = check_value(point, &entry->data[i].value);
        }
        if (err) {
            k_free(entry);
            return -EINVAL;
        }
    }
    /* 整表校验及初值设置成功后才发布。 */
    entry->next = tables;
    tables = entry;
    return 0;
}

/* 回调只操作副本；最终校验及存储成功后才更新点数据。 */
static int save_value(const dp_point_config_t *point, dp_point_data_t *stored, const value_t *value)
{
    int err = check_value(point, value);
    if (err) {
        return err;
    }
    k_mutex_lock(&store_lock, K_FOREVER);
    stored->value = *value;
    stored->updated_at_s = dp_timestamp_s();
    k_mutex_unlock(&store_lock);
    return 0;
}

static int read_point(uint16_t device_id,
    const dp_point_config_t *point,
    dp_point_data_t *stored,
    dp_point_data_t *state,
    size_t signal_index)
{
    k_mutex_lock(&store_lock, K_FOREVER);
    *state = *stored;
    k_mutex_unlock(&store_lock);
    if (!point->read_callback) {
        return 0;
    }
    const dp_callback_context_t context = {
        .device_id = device_id,
        .signal_key = point->signal_key,
        .signal_index = signal_index,
        .is_write = false,
        .value = &state->value,
    };
    int err = point->read_callback(&context);
    if (err) {
        return err;
    }
    return save_value(point, stored, &state->value);
}

int dp_set_para(uint16_t device_id, uint16_t signal_key, uint8_t size, const uint8_t *bytes)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!bytes) {
        return -EINVAL;
    }
    const dp_point_config_t *p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    if (size != p->size) {
        return -EMSGSIZE;
    }
    value_t value;
    unpack(&value, bytes, size);
    if (p->write_callback) {
        const dp_callback_context_t context = {
            .device_id = device_id,
            .signal_key = signal_key,
            .signal_index = signal_index,
            .is_write = true,
            .value = &value,
        };
        err = p->write_callback(&context);
        if (err) {
            return err;
        }
    }
    return save_value(p, stored, &value);
}

int dp_get_para(uint16_t device_id, uint16_t signal_key, uint8_t capacity, uint8_t *bytes)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!bytes) {
        return -EINVAL;
    }
    const dp_point_config_t *p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    if (capacity < p->size) {
        return -EMSGSIZE;
    }
    dp_point_data_t state;
    err = read_point(device_id, p, stored, &state, signal_index);
    if (err) {
        return err;
    }
    pack(bytes, &state.value, p->size);
    return capacity;
}
