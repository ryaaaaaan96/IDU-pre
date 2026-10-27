#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(dp, LOG_LEVEL_INF);

struct stored_point {
    int64_t sampled_at;
    uint16_t *words;
    uint32_t generation;
    int error;
    uint16_t quality;
};

static const struct dp_model_cfg *models;
static size_t model_count;
static size_t point_count;
static struct stored_point states[CONFIG_DP_POINTS_MAX_POINTS];
static uint32_t generation;
static atomic_t ready;
static atomic_t stopped;
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

static int row_view(uint16_t dev, const DM_SIG_CFG_S *row, struct dp_point_cfg *p)
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

const struct dp_point_cfg *dp_config_point(size_t index, struct dp_point_cfg *scratch)
{
    if (!models || !scratch) { return NULL; }
    for (size_t i = 0; i < model_count; ++i) {
        if (index < models[i].point_count) {
            return row_view(models[i].device_id, &models[i].points[index], scratch) ?
                   NULL : scratch;
        }
        index -= models[i].point_count;
    }
    return NULL;
}

int dp_config_index(struct dp_key key)
{
    size_t offset = 0;
    if (!key.dev_id || !key.sig_id) { return -EINVAL; }
    for (size_t i = 0; i < model_count; ++i) {
        if (models[i].device_id != key.dev_id) {
            offset += models[i].point_count;
            continue;
        }
        for (size_t n = 0; n < models[i].point_count; ++n) {
            if (models[i].points[n].sig_id == key.sig_id) { return offset + n; }
        }
        return -ENOENT;
    }
    return -ENOENT;
}

size_t dp_point_count(void) { return point_count; }
bool dp_ready(void) { return atomic_get(&ready) != 0; }

static int validate_models(const struct dp_model_cfg *cfg, size_t count)
{
    if (!cfg || !count || count > CONFIG_DP_POINTS_MAX_POINTS) { return -EINVAL; }
    size_t total = 0, words = 0, params = 0;
    for (size_t i = 0; i < count; ++i) {
        const struct dp_model_cfg *model = &cfg[i];
        if (!model->device_id || !model->points || !model->point_count ||
            model->point_count > CONFIG_DP_POINTS_MAX_POINTS - total) {
            return -EINVAL;
        }
        total += model->point_count;
        for (size_t n = 0; n < model->point_count; ++n) {
            struct dp_point_cfg p;
            if (row_view(model->device_id, &model->points[n], &p)) { return -EINVAL; }
            words += p.width;
            if (words > CONFIG_DP_POINTS_VALUE_WORDS) { return -ENOSPC; }
            if (p.source == DP_SOURCE_PARAMETER &&
                ++params > CONFIG_DP_PARAMS_MAX_COUNT) { return -ENOSPC; }
            for (size_t m = 0; m < n; ++m) {
                if (model->points[m].sig_id == model->points[n].sig_id) {
                    return -EEXIST;
                }
            }
        }
        for (size_t m = 0; m < i; ++m) {
            if (cfg[m].device_id == model->device_id) { return -EEXIST; }
        }
    }
    return 0;
}

int dp_start(const struct dp_model_cfg *cfg, size_t count, uint8_t params_area)
{
    if (atomic_get(&ready)) { return -EALREADY; }
    if (dp_stopped()) { return -ECANCELED; }
    int err = validate_models(cfg, count);
    if (err) { return err; }
    size_t words = 0;
    for (size_t i = 0; i < count; ++i) {
        for (size_t n = 0; n < cfg[i].point_count; ++n) {
            struct dp_point_cfg p;
            if (row_view(cfg[i].device_id, &cfg[i].points[n], &p)) { return -EINVAL; }
            words += p.width;
        }
    }
    uint16_t *storage = k_malloc(words * sizeof(*storage));
    if (!storage) { return -ENOMEM; }
    models = cfg;
    model_count = count;
    point_count = 0;
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        for (size_t n = 0; n < cfg[i].point_count; ++n) {
            struct dp_point_cfg p;
            if (row_view(cfg[i].device_id, &cfg[i].points[n], &p)) {
                k_free(storage); models = NULL; model_count = 0; point_count = 0;
                return -EINVAL;
            }
            states[point_count].words = storage + offset;
            states[point_count].sampled_at = 0;
            states[point_count].generation = 0;
            states[point_count].error = p.source == DP_SOURCE_CONSTANT ? 0 : -ENODATA;
            states[point_count].quality = p.source == DP_SOURCE_CONSTANT ?
                                          DP_QUALITY_GOOD : DP_QUALITY_UNINITIALIZED;
            memcpy(states[point_count].words, p.initial.words, p.width * 2);
            offset += p.width;
            ++point_count;
        }
    }
    err = params_area == DP_NO_PARAMS ? -ENODEV : dp_params_init(params_area);
    if (err && params_area != DP_NO_PARAMS) {
        LOG_WRN("Parameter storage unavailable (%d)", err);
    }
    generation = 0;
    atomic_set(&ready, 1);
    return 0;
}

int dp_read(struct dp_key key, struct dp_point_state *state)
{
    if (dp_stopped()) { return -ECANCELED; }
    if (!atomic_get(&ready) || !state || k_is_in_isr()) { return -EINVAL; }
    int index = dp_config_index(key);
    if (index < 0) { return index; }
    struct dp_point_cfg p;
    if (!dp_config_point(index, &p)) { return -ENOENT; }
    if (p.read) {
        *state = (struct dp_point_state){
            .quality = DP_QUALITY_GOOD,
            .sampled_at = k_uptime_get(),
            .generation = ++generation,
        };
        return p.read(key, state);
    }
    if (p.source == DP_SOURCE_PARAMETER) { return dp_params_read(key, state); }
    k_mutex_lock(&store_lock, K_FOREVER);
    *state = (struct dp_point_state){
        .sampled_at = states[index].sampled_at,
        .generation = states[index].generation,
        .error = states[index].error,
        .quality = states[index].quality,
    };
    memcpy(state->value.words, states[index].words, p.width * 2);
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_set_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t size, uint8_t *bytes)
{
    if (dp_stopped()) { return -ECANCELED; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!bytes) { return -EINVAL; }
    struct dp_key key = {dev_id, sig_id};
    int index = dp_config_index(key);
    if (index < 0) { return index; }
    struct dp_point_cfg p;
    if (!dp_config_point(index, &p)) { return -ENOENT; }
    if (size != p.size_bytes) { return -EMSGSIZE; }
    if (p.source == DP_SOURCE_CONSTANT) { return -EACCES; }
    struct dp_value value;
    unpack(&value, bytes, size);
    int err = dp_check_value(&p, &value);
    if (err) { return err; }
    if (p.handler) { return p.handler(key, &value); }
    if (p.source == DP_SOURCE_PARAMETER) { return dp_params_write(key, &value); }
    k_mutex_lock(&store_lock, K_FOREVER);
    memcpy(states[index].words, value.words, p.width * 2);
    states[index].sampled_at = k_uptime_get();
    states[index].quality = DP_QUALITY_GOOD;
    states[index].error = 0;
    states[index].generation = ++generation;
    k_mutex_unlock(&store_lock);
    return 0;
}

int dp_get_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *bytes)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!bytes) { return -EINVAL; }
    struct dp_key key = {dev_id, sig_id};
    int index = dp_config_index(key);
    if (index < 0) { return index; }
    struct dp_point_cfg p;
    if (!dp_config_point(index, &p)) { return -ENOENT; }
    if (capacity < p.size_bytes) { return -EMSGSIZE; }
    struct dp_point_state state;
    int err = dp_read(key, &state);
    if (err) { return err; }
    pack(bytes, &state.value, p.size_bytes);
    return capacity;
}

void dp_stop(void) { atomic_set(&stopped, 1); }
bool dp_stopped(void) { return atomic_get(&stopped) != 0; }
