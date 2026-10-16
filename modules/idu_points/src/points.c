#include <idu/points.h>
#if defined(CONFIG_IDU_PARAMS)
#include <idu/params.h>
#include <idu/process.h>
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(idu_points, LOG_LEVEL_INF);

static const struct idu_equip_cfg *equipment;
struct stored_point {
    uint16_t *words;
    int64_t sampled_at;
    uint32_t generation;
    int error;
    uint16_t quality;
};
static struct stored_point states[CONFIG_IDU_POINTS_MAX_POINTS];
static uint16_t point_words[CONFIG_IDU_POINTS_VALUE_WORDS];
static uint32_t generation;
static atomic_t ready;
static atomic_t last_progress;
static uint32_t published_ticket;
static atomic_t consumed_ticket;
K_MUTEX_DEFINE(store_lock);
K_MUTEX_DEFINE(publish_lock);
K_SEM_DEFINE(sample_slots, CONFIG_IDU_POINTS_QUEUE_DEPTH, CONFIG_IDU_POINTS_QUEUE_DEPTH);
static k_tid_t publisher;
/* zbus needs one temporary buffer and one clone during each publication.
 * Reserved slots + serialized publishers leave these two buffers available.
 */
NET_BUF_POOL_FIXED_DEFINE(sample_pool, CONFIG_IDU_POINTS_QUEUE_DEPTH + 2,
                         sizeof(struct idu_point_batch), sizeof(struct zbus_channel *), NULL);
NET_BUF_POOL_FIXED_DEFINE(notification_pool, 2, sizeof(struct idu_processed_event),
                         sizeof(struct zbus_channel *), NULL);

static bool batch_valid(const void *message, size_t size);
ZBUS_MSG_SUBSCRIBER_DEFINE(idu_processor);
ZBUS_CHAN_DEFINE(idu_samples_chan, struct idu_point_batch, batch_valid, NULL,
                 ZBUS_OBSERVERS(idu_processor), ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(idu_processed_chan, struct idu_processed_event, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* 4.4.2 checks this global limit even when using an isolated fixed-size pool. */
#if defined(CONFIG_ZBUS_MSG_SUBSCRIBER_BUF_ALLOC_STATIC)
BUILD_ASSERT(sizeof(struct idu_point_batch) <=
             CONFIG_ZBUS_MSG_SUBSCRIBER_NET_BUF_STATIC_DATA_SIZE);
#endif

static bool same_key(struct idu_key a, struct idu_key b)
{
    return a.dev_id == b.dev_id && a.sig_id == b.sig_id;
}

static int point_index(const struct idu_equip_cfg *cfg, struct idu_key key)
{
    for (size_t i = 0; i < cfg->point_count; ++i) {
        if (same_key(cfg->points[i].key, key)) {
            return (int)i;
        }
    }
    return -ENOENT;
}

static int64_t number(const struct idu_point_cfg *p, const struct idu_value *v)
{
    uint32_t u32 = v->words[0] | ((uint32_t)v->words[1] << 16);
    switch (p->type) {
    case IDU_U8:
    case IDU_U16: return v->words[0];
    case IDU_S16: return (int16_t)v->words[0];
    case IDU_U32: return u32;
    case IDU_S32: return (int32_t)u32;
    default: return 0;
    }
}

static bool value_in_range(const struct idu_point_cfg *p, const struct idu_value *v)
{
    int64_t n = number(p, v);
    return p->type == IDU_WORDS || ((p->type != IDU_U8 || n <= UINT8_MAX) && n >= p->minimum && n <= p->maximum);
}

static bool valid_fc(uint8_t function)
{
    return function == 3 || function == 4;
}

int idu_points_validate(const struct idu_equip_cfg *cfg)
{
    if (!cfg || !cfg->points || !cfg->point_count ||
        cfg->point_count > CONFIG_IDU_POINTS_MAX_POINTS ||
        cfg->collect_count > CONFIG_IDU_POINTS_MAX_POLLS ||
        cfg->device_count > CONFIG_IDU_DEVICE_COUNT ||
        (cfg->device_count && !cfg->devices) ||
        (cfg->collect_count && !cfg->collect) ||
        (cfg->transmit_count && !cfg->transmit)) {
        return -EINVAL;
    }
    size_t words_needed = 0;
    for (size_t i = 0; i < cfg->point_count; ++i) {
        const struct idu_point_cfg *p = &cfg->points[i];
        if (!p->key.dev_id || !p->key.sig_id || !p->width ||
            p->width > IDU_POINT_WORDS || (unsigned)p->type > IDU_U8 ||
            (unsigned)p->source > IDU_SOURCE_PARAMETER || p->minimum > p->maximum ||
            ((p->type == IDU_U8 || p->type == IDU_U16 || p->type == IDU_S16) && p->width != 1) ||
            ((p->type == IDU_U32 || p->type == IDU_S32) && p->width != 2) ||
            (p->source == IDU_SOURCE_CONSTANT &&
             (p->process || !value_in_range(p, &p->initial)))) {
            LOG_ERR("Invalid point %u:%u type=%u width=%u",p->key.dev_id,p->key.sig_id,p->type,p->width);
            return -EINVAL;
        }
        words_needed += p->width;
        if (words_needed > ARRAY_SIZE(point_words)) { return -ENOSPC; }
        if (point_index(cfg, p->key) != (int)i) {
            return -EEXIST;
        }
    }
    for (size_t i = 0; i < cfg->device_count; ++i) {
        const struct idu_device_cfg *d = &cfg->devices[i];
        if (!d->dev_id || d->bus_id >= CONFIG_IDU_BUS_COUNT || d->unit_id < 1 || d->unit_id > 247) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (cfg->devices[j].dev_id == d->dev_id ||
                (cfg->devices[j].bus_id == d->bus_id && cfg->devices[j].unit_id == d->unit_id)) {
                return -EEXIST;
            }
        }
    }
    for (size_t i = 0; i < cfg->collect_count; ++i) {
        const struct idu_collect_cfg *c = &cfg->collect[i];
        bool found = false;
        for (size_t d = 0; d < cfg->device_count; ++d) {
            found |= cfg->devices[d].dev_id == c->dev_id;
        }
        if (!found || (!c->model_protocol && !valid_fc(c->function)) || !c->count ||
            c->count > IDU_POLL_REGS || (uint32_t)c->address + c->count > 65536 ||
            c->count * 2 + 1 > sizeof(((struct modbus_adu *)0)->data) ||
            (!c->period_ms && !c->init_only) || c->priority > 3 || !c->fields || !c->field_count ||
            c->field_count > IDU_BATCH_POINTS) {
            LOG_ERR("Invalid poll %u device=%u",(unsigned)i,c->dev_id);
            return -EINVAL;
        }
        for (size_t f = 0; f < c->field_count; ++f) {
            const struct idu_collect_field *field = &c->fields[f];
            int index = point_index(cfg, (struct idu_key){c->dev_id, field->sig_id});
            if (index < 0 || !field->width ||
                field->width != cfg->points[index].width ||
                cfg->points[index].source != IDU_SOURCE_MODBUS ||
                field->offset + field->width > c->count ||
                (unsigned)field->order > IDU_HIGH_WORD_BYTE_SWAP) {
                LOG_ERR("Invalid poll field %u:%u width=%u",c->dev_id,field->sig_id,field->width);
                return -EINVAL;
            }
            /* Exactly one collection owner for each signal. */
            for (size_t b = 0; b <= i; ++b) {
                const struct idu_collect_cfg *other = &cfg->collect[b];
                size_t end = b == i ? f : other->field_count;
                for (size_t n = 0; n < end; ++n) {
                    if (other->dev_id == c->dev_id &&
                        other->fields[n].sig_id == field->sig_id) {
                        return -EEXIST;
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < cfg->transmit_count; ++i) {
        const struct idu_transmit_cfg *t = &cfg->transmit[i];
        if (!valid_fc(t->function) || !t->count ||
            (uint32_t)t->address + t->count > 65536 ||
            (unsigned)t->kind > IDU_MAP_ZERO || (unsigned)t->order > IDU_HIGH_WORD_BYTE_SWAP) {
            return -EINVAL;
        }
        if (t->kind != IDU_MAP_ZERO) {
            int index = point_index(cfg, t->key);
            if (index < 0 || (t->kind == IDU_MAP_VALUE &&
                             t->count > cfg->points[index].width) ||
                (t->kind == IDU_MAP_QUALITY && t->count != 1)) {
                LOG_ERR("Invalid mapping %u device=%u sig=%u count=%u",t->address,t->key.dev_id,t->key.sig_id,t->count);
                return -EINVAL;
            }
        }
        for (size_t j = 0; j < i; ++j) {
            const struct idu_transmit_cfg *other = &cfg->transmit[j];
            if (t->function == other->function &&
                t->address < (uint32_t)other->address + other->count &&
                other->address < (uint32_t)t->address + t->count) {
                if (!cfg->exact_overlapping_reads || t->address == other->address) {
                    return -EEXIST;
                }
            }
        }
    }
#if defined(CONFIG_IDU_PARAMS)
    return idu_params_validate(cfg);
#else
    for (size_t i = 0; i < cfg->point_count; ++i) {
        if (cfg->points[i].source == IDU_SOURCE_PARAMETER) {
            return -ENOTSUP;
        }
    }
    return 0;
#endif
}

int idu_points_init(const struct idu_equip_cfg *cfg)
{
    if (atomic_get(&ready)) {
        return -EALREADY;
    }
    int err = idu_points_validate(cfg);
    if (err) {
        return err;
    }
    equipment = cfg;
    zbus_chan_set_msg_sub_pool(&idu_samples_chan, &sample_pool);
    zbus_chan_set_msg_sub_pool(&idu_processed_chan, &notification_pool);
    size_t offset = 0;
    for (size_t i = 0; i < cfg->point_count; ++i) {
        states[i].words = point_words + offset;
        memcpy(states[i].words, cfg->points[i].initial.words, cfg->points[i].width * 2);
        offset += cfg->points[i].width;
        states[i].quality = cfg->points[i].source == IDU_SOURCE_CONSTANT ?
                            IDU_QUALITY_GOOD : IDU_QUALITY_UNINITIALIZED;
        states[i].error = cfg->points[i].source == IDU_SOURCE_CONSTANT ? 0 : -ENODATA;
    }
    atomic_set(&last_progress, k_uptime_get_32());
    atomic_set(&ready, 1);
    return 0;
}

static int validate_batch(const struct idu_point_batch *batch)
{
    if (!atomic_get(&ready)) {
        return -EACCES;
    }
    if (!batch || !batch->count || batch->count > IDU_BATCH_POINTS ||
        (batch->source != IDU_SOURCE_LOCAL && batch->source != IDU_SOURCE_MODBUS) ||
        (batch->flags & ~IDU_QUALITY_SIMULATED)) {
        return -EINVAL;
    }
    /* Validate the complete batch before publishing any changes. */
    for (size_t i = 0; i < batch->count; ++i) {
        const struct idu_point_update *update = &batch->points[i];
        int index = point_index(equipment, update->key);
        if (index < 0) {
            return index;
        }
        if (update->width != equipment->points[index].width ||
            equipment->points[index].source != batch->source) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (same_key(update->key, batch->points[j].key)) {
                return -EEXIST;
            }
        }
    }
    return 0;
}

static bool batch_valid(const void *message, size_t size)
{
    const struct idu_point_batch *batch = message;
    return publisher == k_current_get() && size == sizeof(*batch) &&
           validate_batch(batch) == 0 &&
           batch->sampled_at >= 0 && batch->sampled_at <= k_uptime_get();
}

int idu_points_publish(struct idu_point_batch *batch, k_timeout_t timeout)
{
    int err = validate_batch(batch);
    if (err) {
        return err;
    }
    batch->sampled_at = k_uptime_get();
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    err = k_sem_take(&sample_slots, sys_timepoint_timeout(deadline));
    if (err) {
        return err;
    }
    err = k_mutex_lock(&publish_lock, sys_timepoint_timeout(deadline));
    if (!err) {
        batch->ticket = ++published_ticket;
        if (!batch->ticket) { batch->ticket = ++published_ticket; }
        publisher = k_current_get();
        err = zbus_chan_pub(&idu_samples_chan, batch, sys_timepoint_timeout(deadline));
        publisher = NULL;
        k_mutex_unlock(&publish_lock);
    }
    if (err) {
        k_sem_give(&sample_slots);
    }
    return err;
}

int idu_points_wait_processed(uint32_t ticket, k_timeout_t timeout)
{
    if (!ticket || k_is_in_isr()) { return -EINVAL; }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    while ((int32_t)((uint32_t)atomic_get(&consumed_ticket) - ticket) < 0) {
        if (sys_timepoint_expired(deadline)) { return -ETIMEDOUT; }
        k_sleep(K_MSEC(1));
    }
    return 0;
}

static uint16_t effective_quality(size_t index)
{
    uint16_t quality = states[index].quality;
    const struct idu_point_cfg *p = &equipment->points[index];
    if ((quality & 0xffU) == IDU_QUALITY_GOOD && p->source != IDU_SOURCE_CONSTANT &&
        p->stale_ms && k_uptime_get() - states[index].sampled_at >= p->stale_ms) {
        quality = (quality & IDU_QUALITY_SIMULATED) | IDU_QUALITY_STALE;
    }
    return quality;
}

int idu_points_read(struct idu_key key, struct idu_point_state *state)
{
    if (!atomic_get(&ready) || !state) {
        return -EINVAL;
    }
    int index = point_index(equipment, key);
    if (index < 0) {
        return index;
    }
#if defined(CONFIG_IDU_PARAMS)
    if (equipment->points[index].source == IDU_SOURCE_PARAMETER) {
        return idu_params_read(key, state);
    }
#endif
    k_mutex_lock(&store_lock, K_FOREVER);
    *state = (struct idu_point_state){.sampled_at = states[index].sampled_at,
        .generation = states[index].generation, .error = states[index].error};
    memcpy(state->value.words, states[index].words, equipment->points[index].width * 2);
    state->quality = effective_quality(index);
    k_mutex_unlock(&store_lock);
    return 0;
}

bool idu_points_healthy(uint32_t max_idle_ms)
{
    return atomic_get(&ready) &&
           (uint32_t)(k_uptime_get_32() - (uint32_t)atomic_get(&last_progress)) <
           max_idle_ms;
}

int idu_points_read_register(uint8_t function, uint16_t address, uint16_t *value)
{
    if (!atomic_get(&ready) || !value) {
        return -EINVAL;
    }
    if (equipment->exact_overlapping_reads) {
        size_t matches = 0;
        for (size_t i = 0; i < equipment->transmit_count; ++i) {
            const struct idu_transmit_cfg *t = &equipment->transmit[i];
            if (t->function == function && address >= t->address &&
                (uint32_t)address < (uint32_t)t->address + t->count) { ++matches; }
        }
        if (matches > 1) { return -ERANGE; }
    }
    for (size_t i = 0; i < equipment->transmit_count; ++i) {
        const struct idu_transmit_cfg *t = &equipment->transmit[i];
        if (t->function != function || address < t->address ||
            (uint32_t)address >= (uint32_t)t->address + t->count) {
            continue;
        }
        if (t->kind == IDU_MAP_ZERO) {
            *value = 0;
            return 0;
        }
        struct idu_point_state state;
#if defined(CONFIG_IDU_PARAMS)
        int err = idu_process_read(t->key, 0, &state);
#else
        int err = idu_points_read(t->key, &state);
#endif
        if (err) {
            return err;
        }
        if (t->kind == IDU_MAP_QUALITY) {
            *value = state.quality;
            return 0;
        }
        if ((state.quality & 0xffU) != IDU_QUALITY_GOOD) {
            return -EIO;
        }
        unsigned offset = address - t->address;
        *value = idu_order_word(state.value.words, t->count, offset, t->order);
        return 0;
    }
    return -ENOTSUP;
}

static void process_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    const struct zbus_channel *channel;
    struct idu_point_batch batch;
    while (true) {
        int err = zbus_sub_wait_msg(&idu_processor, &channel, &batch, K_FOREVER);
        if (err) {
            continue;
        }
        k_sem_give(&sample_slots);
        if (channel != &idu_samples_chan || !atomic_get(&ready)) {
            continue;
        }
        uint16_t quality[IDU_BATCH_POINTS];
        bool callbacks = false, all_valid = true;
        k_mutex_lock(&store_lock, K_FOREVER);
        for (size_t i = 0; i < batch.count; ++i) {
            struct idu_point_update *u = &batch.points[i];
            int n = point_index(equipment, u->key);
            const struct idu_point_cfg *p = &equipment->points[n];
            quality[i] = u->error ? IDU_QUALITY_COMM_ERROR : IDU_QUALITY_GOOD;
            if (!u->error) {
                struct idu_value old = {0};
                memcpy(old.words, states[n].words, p->width * 2);
                if (p->process) { u->error = p->process(p, &u->value, &old); }
                if (!u->error && !value_in_range(p, &u->value)) { u->error = -ERANGE; }
                if (u->error) { quality[i] = IDU_QUALITY_BAD_VALUE; }
            }
            all_valid &= !u->error;
            callbacks |= p->handler != NULL;
        }
        k_mutex_unlock(&store_lock);
#if defined(CONFIG_IDU_PARAMS)
        if (callbacks && all_valid) {
            err = idu_process_sample(&batch);
            if (!err) {
                atomic_set(&last_progress, k_uptime_get_32());
                atomic_set(&consumed_ticket, batch.ticket);
                continue;
            }
            /* Callback validation rejects this transaction before the usual
             * sample commit. Expose invalid input quality, retaining old values. */
            for (size_t i = 0; i < batch.count; ++i) {
                batch.points[i].error = err; quality[i] = IDU_QUALITY_BAD_VALUE;
            }
        }
#else
        ARG_UNUSED(callbacks); ARG_UNUSED(all_valid);
#endif
        k_mutex_lock(&store_lock, K_FOREVER);
        ++generation;
        for (size_t i = 0; i < batch.count; ++i) {
            const struct idu_point_update *u = &batch.points[i];
            struct stored_point *state = &states[point_index(equipment, u->key)];
            if (!u->error) { memcpy(state->words, u->value.words, u->width * 2); }
            state->sampled_at = batch.sampled_at;
            state->quality = quality[i] | batch.flags;
            state->error = u->error;
            state->generation = generation;
        }
        k_mutex_unlock(&store_lock);
        atomic_set(&last_progress, k_uptime_get_32());
        const struct idu_processed_event event = {
            .generation = generation, .changed = batch.count,
        };
        err = zbus_chan_pub(&idu_processed_chan, &event, K_NO_WAIT);
        if (err) {
            LOG_WRN("Processed notification lost: %d", err);
        }
        atomic_set(&consumed_ticket, batch.ticket);
    }
}

K_THREAD_DEFINE(idu_process_tid, CONFIG_IDU_POINTS_STACK_SIZE,
                process_thread, NULL, NULL, NULL, 5, 0, 0);

const struct idu_equip_cfg *idu_points_config(void) { return equipment; }

const struct idu_point_cfg *idu_points_find(struct idu_key key)
{
    int i = equipment ? point_index(equipment, key) : -ENOENT;
    return i < 0 ? NULL : &equipment->points[i];
}

int idu_points_check_value(const struct idu_point_cfg *p, const struct idu_value *v)
{
    if (!p || !v) { return -EINVAL; }
    if (p->type == IDU_U8 && v->words[0] > UINT8_MAX) { return -ERANGE; }
    return value_in_range(p, v) ? 0 : -ERANGE;
}

int idu_points_commit_sample(const struct idu_point_update *values, size_t count,
                              uint16_t flags, int64_t sampled_at)
{
    if (!equipment || !values || !count) { return -EINVAL; }
    for (size_t i = 0; i < count; ++i) {
        const struct idu_point_cfg *p = idu_points_find(values[i].key);
        if (!p || p->source == IDU_SOURCE_PARAMETER || p->source == IDU_SOURCE_CONSTANT ||
            values[i].width != p->width || values[i].error) { return -EINVAL; }
        int err = idu_points_check_value(p, &values[i].value);
        if (err) { return err; }
    }
    k_mutex_lock(&store_lock, K_FOREVER);
    ++generation;
    for (size_t i = 0; i < count; ++i) {
        int n = point_index(equipment, values[i].key);
        memcpy(states[n].words, values[i].value.words, values[i].width * 2);
        states[n].quality = IDU_QUALITY_GOOD | flags;
        states[n].sampled_at = sampled_at;
        states[n].error = 0;
        states[n].generation = generation;
    }
    struct idu_processed_event event = {.generation = generation, .changed = count};
    k_mutex_unlock(&store_lock);
    zbus_chan_pub(&idu_processed_chan, &event, K_NO_WAIT);
    return 0;
}

int idu_points_commit(const struct idu_point_update *values, size_t count)
{
    return idu_points_commit_sample(values, count, 0, k_uptime_get());
}
