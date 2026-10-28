#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp, LOG_LEVEL_INF);

/* 表描述及点状态均由调用方静态提供，模块只维护登记链表。 */
static struct dp_table *tables;
static size_t param_points;
static uint32_t generation;
static K_MUTEX_DEFINE(store_lock);

static int64_t number(const struct dp_point_cfg *p, const struct dp_value *v)
{
    uint32_t u = v->words[0] | ((uint32_t)v->words[1] << 16);

    switch (p->type) {
    case DP_S16: return (int16_t)v->words[0];
    case DP_S32: return (int32_t)u;
    case DP_U32: return u;
    default: return v->words[0];
    }
}

int dp_check_value(const struct dp_point_cfg *p, const struct dp_value *v)
{
    if (!p || !v) { return -EINVAL; }
    if (p->type == DP_U8 && (v->words[0] > UINT8_MAX ||
        (p->size_bytes == 1 && (v->words[0] & 0xff00U)))) { return -ERANGE; }
    if (p->type == DP_WORDS) {
        if ((p->size_bytes & 1U) &&
            (p->size_bytes / 2 >= DP_POINT_WORDS ||
             (v->words[p->size_bytes / 2] & 0xff00U))) { return -ERANGE; }
        return 0;
    }
    int64_t n = number(p, v);
    return n < p->minimum || n > p->maximum ? -ERANGE : 0;
}

static void unpack(struct dp_value *value, const uint8_t *bytes, size_t size)
{
    *value = (struct dp_value){0};
    for (size_t i = 0; i < size; ++i) {
        value->words[i / 2] |= (uint16_t)bytes[i] << (8 * (i % 2));
    }
}

static void pack(uint8_t *bytes, const struct dp_value *value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = (uint8_t)(value->words[i / 2] >> (8 * (i % 2)));
    }
}

int dp_describe_point(uint16_t dev, const DM_SIG_CFG_S *row, struct dp_point_cfg *p)
{
    if (!row || !row->sig_id || !row->sig_size ||
        row->sig_size > DP_POINT_WORDS * 2) { return -EINVAL; }
    *p = (struct dp_point_cfg){
        .key = {dev, row->sig_id},
        .size_bytes = row->sig_size,
        .width = (row->sig_size + 1) / 2,
        .flags = row->sig_flag,
        .handler = row->handler,
        .read = row->read,
    };
    switch (row->data_type) {
    case DATA_TYPE_U8:
        if (row->sig_size != 1) { return -EINVAL; }
        p->type = DP_U8; p->minimum = 0; p->maximum = UINT8_MAX; break;
    case DATA_TYPE_U16:
        if (row->sig_size != 2) { return -EINVAL; }
        p->type = DP_U16; p->minimum = 0; p->maximum = UINT16_MAX; break;
    case DATA_TYPE_S16:
        if (row->sig_size != 2) { return -EINVAL; }
        p->type = DP_S16; p->minimum = INT16_MIN; p->maximum = INT16_MAX; break;
    case DATA_TYPE_U32:
        if (row->sig_size != 4) { return -EINVAL; }
        p->type = DP_U32; p->minimum = 0; p->maximum = UINT32_MAX; break;
    case DATA_TYPE_S32:
        if (row->sig_size != 4) { return -EINVAL; }
        p->type = DP_S32; p->minimum = INT32_MIN; p->maximum = INT32_MAX; break;
    case DATA_TYPE_STRING:
    case DATA_TYPE_U8ARRAY:
    case DATA_TYPE_U16ARRAY:
        if (row->data_type == DATA_TYPE_U16ARRAY && (row->sig_size & 1U)) {
            return -EINVAL;
        }
        p->type = DP_WORDS; break;
    default:
        return -ENOTSUP;
    }
    if (row->sig_flag & FLAG_CONSTANT) {
        if ((row->sig_flag & FLAG_PERSISTENCE) || row->handler) { return -EINVAL; }
        p->source = DP_SOURCE_CONSTANT;
    } else if (row->sig_flag & FLAG_PERSISTENCE) {
        p->source = DP_SOURCE_PARAMETER;
    } else {
        p->source = DP_SOURCE_LOCAL;
    }
    if (p->source == DP_SOURCE_PARAMETER && (row->handler || row->read)) {
        return -EINVAL;
    }
    if ((row->sig_flag & FLAG_READ_CALLBACK) && !row->read) { return -EINVAL; }
    if (row->sig_flag & FLAG_RANGE_CHECK) {
        if (p->type == DP_WORDS) { return -ENOTSUP; }
        switch (p->type) {
        case DP_U8: p->minimum = row->data.min_value.u16;
                    p->maximum = row->data.max_value.u16;
                    if (p->maximum > UINT8_MAX) { return -ERANGE; } break;
        case DP_U16: p->minimum = row->data.min_value.u16;
                     p->maximum = row->data.max_value.u16; break;
        case DP_S16: p->minimum = row->data.min_value.s16;
                     p->maximum = row->data.max_value.s16; break;
        case DP_U32: p->minimum = row->data.min_value.u32;
                     p->maximum = row->data.max_value.u32; break;
        case DP_S32: p->minimum = row->data.min_value.s32;
                     p->maximum = row->data.max_value.s32; break;
        default: break;
        }
    }
    const uint8_t *bytes = row->sig_size > 4 ? row->data.def_value.pst :
                                                row->data.def_value.str;
    if (bytes) { unpack(&p->initial, bytes, row->sig_size); }
    return dp_check_value(p, &p->initial);
}

const struct dp_table *dp_tables(void) { return tables; }

static int find_point(struct dp_key key, struct dp_point_cfg *point,
                      struct dp_point_state **state)
{
    if (!key.dev_id || !key.sig_id) { return -EINVAL; }
    for (struct dp_table *table = tables; table; table = table->next) {
        if (table->device_id != key.dev_id) { continue; }
        for (size_t i = 0; i < table->point_count; ++i) {
            if (table->points[i].sig_id != key.sig_id) { continue; }
            *state = &table->states[i];
            return dp_describe_point(key.dev_id, &table->points[i], point);
        }
        return -ENOENT;
    }
    return -ENOENT;
}

int dp_register_table(struct dp_table *table)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!table || !table->device_id || !table->points || !table->point_count ||
        !table->states) { return -EINVAL; }
    for (const struct dp_table *item = tables; item; item = item->next) {
        if (item->device_id == table->device_id) { return -EEXIST; }
        if (item->states == table->states) { return -EINVAL; }
    }

    /* 全表通过校验后才写状态、挂接，失败不影响已登记表。 */
    size_t params = 0;
    for (size_t i = 0; i < table->point_count; ++i) {
        struct dp_point_cfg point;
        if (dp_describe_point(table->device_id, &table->points[i], &point)) {
            return -EINVAL;
        }
        if (point.source == DP_SOURCE_PARAMETER) { ++params; }
        for (size_t n = 0; n < i; ++n) {
            if (table->points[n].sig_id == table->points[i].sig_id) { return -EEXIST; }
        }
    }
    if (params > CONFIG_DP_PARAMS_MAX_COUNT - param_points) { return -ENOSPC; }
    if (params && dp_params_mounted()) { return -EBUSY; }

    for (size_t i = 0; i < table->point_count; ++i) {
        struct dp_point_cfg point;
        dp_describe_point(table->device_id, &table->points[i], &point);
        table->states[i] = (struct dp_point_state){
            .value = point.initial,
            .error = point.source == DP_SOURCE_CONSTANT ? 0 : -ENODATA,
            .quality = point.source == DP_SOURCE_CONSTANT ?
                       DP_QUALITY_GOOD : DP_QUALITY_UNINITIALIZED,
        };
    }
    table->next = tables;
    tables = table;
    param_points += params;
    return 0;
}

static int read_point(const struct dp_point_cfg *point,
                      const struct dp_point_state *stored, struct dp_point_state *state)
{
    if (point->read) {
        k_mutex_lock(&store_lock, K_FOREVER);
        uint32_t current = ++generation;
        k_mutex_unlock(&store_lock);
        *state = (struct dp_point_state){
            .quality = DP_QUALITY_GOOD,
            .sampled_at = k_uptime_get(),
            .generation = current,
        };
        return point->read(point->key, state);
    }
    if (point->source == DP_SOURCE_PARAMETER) { return dp_params_read(point->key, state); }
    k_mutex_lock(&store_lock, K_FOREVER);
    *state = *stored;
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_set_para(uint16_t dev_id, uint16_t sig_id, uint8_t size, const uint8_t *bytes)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!bytes) { return -EINVAL; }
    struct dp_key key = {dev_id, sig_id};
    struct dp_point_cfg p;
    struct dp_point_state *stored;
    int err = find_point(key, &p, &stored);
    if (err) { return err; }
    if (size != p.size_bytes) { return -EMSGSIZE; }
    if (p.source == DP_SOURCE_CONSTANT) { return -EACCES; }
    struct dp_value value;
    unpack(&value, bytes, size);
    err = dp_check_value(&p, &value);
    if (err) { return err; }
    if (p.handler) { return p.handler(key, &value); }
    if (p.source == DP_SOURCE_PARAMETER) { return dp_params_write(key, &value); }
    k_mutex_lock(&store_lock, K_FOREVER);
    stored->value = value;
    stored->sampled_at = k_uptime_get();
    stored->quality = DP_QUALITY_GOOD;
    stored->error = 0;
    stored->generation = ++generation;
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_get_para(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *bytes)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!bytes) { return -EINVAL; }
    struct dp_key key = {dev_id, sig_id};
    struct dp_point_cfg p;
    struct dp_point_state *stored;
    int err = find_point(key, &p, &stored);
    if (err) { return err; }
    if (capacity < p.size_bytes) { return -EMSGSIZE; }
    struct dp_point_state state;
    err = read_point(&p, stored, &state);
    if (err) { return err; }
    pack(bytes, &state.value, p.size_bytes);
    return capacity;
}
