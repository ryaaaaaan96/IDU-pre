#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp, LOG_LEVEL_INF);

/* 单次分配中不会使节点头及点数据总长度溢出的最大点数。 */
#define DP_MAX_ALLOCATABLE_POINTS                                                                  \
    ((SIZE_MAX - sizeof(dp_registered_table_t)) / sizeof(dp_point_data_t))

/* 应用只提供只读点表，设备目录与点数据均由模块私有持有。 */
static dp_registered_table_t *tables;
static size_t param_points;
static K_MUTEX_DEFINE(store_lock);

static int64_t number(const dp_point_cfg_t *p, const value_t *v)
{
    uint32_t u = v->words[0] | ((uint32_t)v->words[1] << 16);

    switch (p->type) {
    case DP_S16:
        return (int16_t)v->words[0];
    case DP_S32:
        return (int32_t)u;
    case DP_U32:
        return u;
    default:
        return v->words[0];
    }
}

int dp_check_value(const dp_point_cfg_t *p, const value_t *v)
{
    if (!p || !v) {
        return -EINVAL;
    }
    if (p->type == DP_U8 &&
        (v->words[0] > UINT8_MAX || (p->size_bytes == 1 && (v->words[0] & 0xff00U)))) {
        return -ERANGE;
    }
    if (p->type == DP_WORDS) {
        if ((p->size_bytes & 1U) &&
            (p->size_bytes / 2 >= DP_POINT_WORDS || (v->words[p->size_bytes / 2] & 0xff00U))) {
            return -ERANGE;
        }
        return 0;
    }
    int64_t n = number(p, v);
    return n < p->minimum || n > p->maximum ? -ERANGE : 0;
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

int dp_describe_point(uint16_t dev, const dp_point_config_t *row, dp_point_cfg_t *p)
{
    if (!row || !row->signal_key || !row->size || row->size > DP_POINT_WORDS * 2) {
        return -EINVAL;
    }
    *p = (dp_point_cfg_t){
        .device_id = dev,
        .signal_key = row->signal_key,
        .size_bytes = row->size,
        .width = (row->size + 1) / 2,
        .flags = row->flags,
        .write_callback = row->write_callback,
        .read_callback = row->read_callback,
    };
    switch (row->type) {
    case VALUE_TYPE_U8:
        if (row->size != 1) {
            return -EINVAL;
        }
        p->type = DP_U8;
        p->minimum = 0;
        p->maximum = UINT8_MAX;
        break;
    case VALUE_TYPE_U16:
        if (row->size != 2) {
            return -EINVAL;
        }
        p->type = DP_U16;
        p->minimum = 0;
        p->maximum = UINT16_MAX;
        break;
    case VALUE_TYPE_S16:
        if (row->size != 2) {
            return -EINVAL;
        }
        p->type = DP_S16;
        p->minimum = INT16_MIN;
        p->maximum = INT16_MAX;
        break;
    case VALUE_TYPE_U32:
        if (row->size != 4) {
            return -EINVAL;
        }
        p->type = DP_U32;
        p->minimum = 0;
        p->maximum = UINT32_MAX;
        break;
    case VALUE_TYPE_S32:
        if (row->size != 4) {
            return -EINVAL;
        }
        p->type = DP_S32;
        p->minimum = INT32_MIN;
        p->maximum = INT32_MAX;
        break;
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_BYTES:
    case VALUE_TYPE_WORDS:
        if (row->type == VALUE_TYPE_WORDS && (row->size & 1U)) {
            return -EINVAL;
        }
        p->type = DP_WORDS;
        break;
    default:
        return -ENOTSUP;
    }
    if (row->flags & FLAG_CONSTANT) {
        if ((row->flags & FLAG_PERSISTENCE) || row->write_callback) {
            return -EINVAL;
        }
        p->source = DP_SOURCE_CONSTANT;
    } else if (row->flags & FLAG_PERSISTENCE) {
        p->source = DP_SOURCE_PARAMETER;
    } else {
        p->source = DP_SOURCE_LOCAL;
    }
    if (p->source == DP_SOURCE_PARAMETER && (row->write_callback || row->read_callback)) {
        return -EINVAL;
    }
    if ((row->flags & FLAG_READ_CALLBACK) && !row->read_callback) {
        return -EINVAL;
    }
    if (row->flags & FLAG_RANGE_CHECK) {
        if (p->type == DP_WORDS) {
            return -ENOTSUP;
        }
        switch (p->type) {
        case DP_U8:
            p->minimum = row->data.minimum.u16;
            p->maximum = row->data.maximum.u16;
            if (p->maximum > UINT8_MAX) {
                return -ERANGE;
            }
            break;
        case DP_U16:
            p->minimum = row->data.minimum.u16;
            p->maximum = row->data.maximum.u16;
            break;
        case DP_S16:
            p->minimum = row->data.minimum.s16;
            p->maximum = row->data.maximum.s16;
            break;
        case DP_U32:
            p->minimum = row->data.minimum.u32;
            p->maximum = row->data.maximum.u32;
            break;
        case DP_S32:
            p->minimum = row->data.minimum.s32;
            p->maximum = row->data.maximum.s32;
            break;
        default:
            break;
        }
    }
    const uint8_t *bytes = row->size > 4 ? row->data.initial.array : row->data.initial.bytes;
    if (bytes) {
        unpack(&p->initial, bytes, row->size);
    }
    return dp_check_value(p, &p->initial);
}

dp_registered_table_t *dp_tables(void)
{
    return tables;
}

static int find_point(uint16_t device_id,
    uint16_t signal_key,
    dp_point_cfg_t *point,
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
            return dp_describe_point(device_id, &table->points[i], point);
        }
        return -ENOENT;
    }
    return -ENOENT;
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

    size_t params = 0;
    for (size_t i = 0; i < table->point_count; ++i) {
        dp_point_cfg_t point;
        if (dp_describe_point(device_id, &table->points[i], &point)) {
            return -EINVAL;
        }
        if (point.source == DP_SOURCE_PARAMETER) {
            ++params;
        }
        for (size_t n = 0; n < i; ++n) {
            if (table->points[n].signal_key == table->points[i].signal_key) {
                return -EEXIST;
            }
        }
    }
    if (params > CONFIG_DP_PARAMS_MAX_COUNT - param_points) {
        return -ENOSPC;
    }
    if (params && dp_params_mounted()) {
        return -EBUSY;
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
        dp_point_cfg_t point;
        dp_describe_point(device_id, &table->points[i], &point);
        entry->data[i].value = point.initial;
    }
    /* 校验、分配和初始化全部完成后才发布。 */
    entry->next = tables;
    tables = entry;
    param_points += params;
    return 0;
}

static int read_point(const dp_point_cfg_t *point,
    dp_point_data_t *stored,
    dp_point_data_t *state,
    size_t signal_index)
{
    if (point->read_callback) {
        k_mutex_lock(&store_lock, K_FOREVER);
        *state = *stored;
        k_mutex_unlock(&store_lock);
        const dp_callback_context_t context = {
            .device_id = point->device_id,
            .signal_key = point->signal_key,
            .signal_index = signal_index,
            .value = &state->value,
        };
        int err = point->read_callback(&context);
        if (!err) {
            err = dp_check_value(point, &state->value);
        }
        if (!err) {
            state->updated_at_s = dp_timestamp_s();
            k_mutex_lock(&store_lock, K_FOREVER);
            *stored = *state;
            k_mutex_unlock(&store_lock);
        }
        return err;
    }
    if (point->source == DP_SOURCE_PARAMETER) {
        return dp_params_read(point->device_id, point->signal_key, state);
    }
    k_mutex_lock(&store_lock, K_FOREVER);
    *state = *stored;
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_set_para(uint16_t device_id, uint16_t signal_key, uint8_t size, const uint8_t *bytes)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!bytes) {
        return -EINVAL;
    }
    dp_point_cfg_t p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    if (size != p.size_bytes) {
        return -EMSGSIZE;
    }
    if (p.source == DP_SOURCE_CONSTANT) {
        return -EACCES;
    }
    value_t value;
    unpack(&value, bytes, size);
    err = dp_check_value(&p, &value);
    if (err) {
        return err;
    }
    if (p.write_callback) {
        const dp_callback_context_t context = {
            .device_id = device_id,
            .signal_key = signal_key,
            .signal_index = signal_index,
            .value = &value,
        };
        return p.write_callback(&context);
    }
    if (p.source == DP_SOURCE_PARAMETER) {
        return dp_params_write(device_id, signal_key, &value);
    }
    k_mutex_lock(&store_lock, K_FOREVER);
    stored->value = value;
    stored->updated_at_s = dp_timestamp_s();
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_get_para(uint16_t device_id, uint16_t signal_key, uint8_t capacity, uint8_t *bytes)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!bytes) {
        return -EINVAL;
    }
    dp_point_cfg_t p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    if (capacity < p.size_bytes) {
        return -EMSGSIZE;
    }
    dp_point_data_t state;
    err = read_point(&p, stored, &state, signal_index);
    if (err) {
        return err;
    }
    pack(bytes, &state.value, p.size_bytes);
    return capacity;
}
