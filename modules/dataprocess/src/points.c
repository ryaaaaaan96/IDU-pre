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

static int64_t number(value_type_t type, const value_t *value)
{
    switch (type) {
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

int dp_check_value(const dp_point_config_t *point, const value_t *value)
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
    if (!(point->flags & FLAG_RANGE_CHECK)) {
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

void dp_initial_value(const dp_point_config_t *point, value_t *value)
{
    const uint8_t *bytes = point->size > 4 ? point->data.initial.array : point->data.initial.bytes;
    *value = (value_t){0};
    if (bytes) {
        unpack(value, bytes, point->size);
    }
}

/* 登记时校验一次；运行期间配置常驻只读，不再解析或复制配置行。 */
static int validate_point(const dp_point_config_t *point)
{
    if (!point->signal_key || !point->size || point->size > DP_POINT_WORDS * 2) {
        return -EINVAL;
    }
    switch (point->type) {
    case VALUE_TYPE_U8:
        if (point->size != 1 ||
            ((point->flags & FLAG_RANGE_CHECK) && point->data.maximum.u16 > UINT8_MAX)) {
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
        if (point->flags & FLAG_RANGE_CHECK) {
            return -ENOTSUP;
        }
        break;
    default:
        return -ENOTSUP;
    }
    if ((point->flags & FLAG_CONSTANT) &&
        ((point->flags & FLAG_PERSISTENCE) || point->write_callback)) {
        return -EINVAL;
    }
    if ((point->flags & FLAG_PERSISTENCE) && (point->write_callback || point->read_callback)) {
        return -EINVAL;
    }
    if ((point->flags & FLAG_READ_CALLBACK) && !point->read_callback) {
        return -EINVAL;
    }
    value_t initial;
    dp_initial_value(point, &initial);
    return dp_check_value(point, &initial);
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
        const dp_point_config_t *point = &table->points[i];
        if (validate_point(point)) {
            return -EINVAL;
        }
        if (point->flags & FLAG_PERSISTENCE) {
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
        dp_initial_value(&table->points[i], &entry->data[i].value);
    }
    /* 校验、分配和初始化全部完成后才发布。 */
    entry->next = tables;
    tables = entry;
    param_points += params;
    return 0;
}

static int read_point(uint16_t device_id,
    const dp_point_config_t *point,
    dp_point_data_t *stored,
    dp_point_data_t *state,
    size_t signal_index)
{
    if (point->read_callback) {
        k_mutex_lock(&store_lock, K_FOREVER);
        *state = *stored;
        k_mutex_unlock(&store_lock);
        const dp_callback_context_t context = {
            .device_id = device_id,
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
    if (point->flags & FLAG_PERSISTENCE) {
        return dp_params_read(device_id, point->signal_key, state);
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
    if (p->flags & FLAG_CONSTANT) {
        return -EACCES;
    }
    value_t value;
    unpack(&value, bytes, size);
    err = dp_check_value(p, &value);
    if (err) {
        return err;
    }
    if (p->write_callback) {
        const dp_callback_context_t context = {
            .device_id = device_id,
            .signal_key = signal_key,
            .signal_index = signal_index,
            .value = &value,
        };
        return p->write_callback(&context);
    }
    if (p->flags & FLAG_PERSISTENCE) {
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
