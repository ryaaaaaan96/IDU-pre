#include <collect/collect.h>
#include "state.h"
#include "../protocols/modbus/client.h"
#include <collect_lib.h>
#include <dataprocess/reference_internal.h>
#include <dataprocess/params.h>
#if defined(CONFIG_DP_PROCESS)
#include <dataprocess/process.h>
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>

struct queued_request {
    struct co_request request;
    co_done_fn done;
    void *user;
    int64_t queued_at;
    bool used;
};
struct bus_state {
    struct co_bus_cfg cfg;
    struct k_mutex lock;
    struct k_sem wake;
    enum co_port_mode mode;
    bool ready, running;
    k_tid_t init_owner;
    struct queued_request queue[CONFIG_CO_QUEUE_DEPTH];
};
static struct bus_state buses[CONFIG_CO_MAX_BUSES];
static const struct dp_equip_cfg *equipment;
static int64_t next_poll[CONFIG_DP_POINTS_MAX_POLLS];
static bool disabled[CONFIG_DP_POINTS_MAX_POLLS];
static bool started;
static struct co_data_callbacks data_callbacks;

int co_set_data_callbacks(const struct co_data_callbacks *callbacks)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (started) { return -EBUSY; }
    data_callbacks = callbacks ? *callbacks : (struct co_data_callbacks){0};
    co_state_set_offline_callback(data_callbacks.offline);
    return 0;
}
static CO_PORT_PF_PROC init_callbacks[CONFIG_CO_MAX_BUSES];
static CO_SET_VALUE_TRANS_CB value_transform;
static struct k_spinlock reference_callbacks_lock;
K_MUTEX_DEFINE(schedule_lock);
NET_BUF_POOL_FIXED_DEFINE(device_event_pool, 2, sizeof(struct co_device_status),
                         sizeof(struct zbus_channel *), NULL);
ZBUS_CHAN_DEFINE(co_device_events, struct co_device_status, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

static int device_index(uint16_t dev_id)
{
    if (!equipment) { return -EACCES; }
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (equipment->devices[i].dev_id == dev_id) { return i; }
    }
    return -ENOENT;
}

static struct bus_state *bus_for(uint16_t dev_id)
{
    int i = device_index(dev_id);
    if (i < 0) { return NULL; }
    struct bus_state *bus = &buses[equipment->devices[i].bus_id];
    return bus->ready ? bus : NULL;
}


static int exchange(uint16_t dev_id, enum co_port_mode mode, struct modbus_adu *adu,
                     struct co_outcome *outcome)
{
    *outcome = (struct co_outcome){0};
    struct bus_state *bus = bus_for(dev_id);
    int d = device_index(dev_id);
    if (!bus || !adu || (unsigned)mode >= CO_PORT_SLEEP || adu->length > sizeof(adu->data)) { return -EINVAL; }
    if (k_current_get() == &k_sys_work_q.thread || k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&bus->lock, K_FOREVER);
    if ((bus->mode != mode && !(bus->mode == CO_PORT_INIT && bus->init_owner == k_current_get())) ||
        (bus->init_owner && bus->init_owner != k_current_get())) {
        k_mutex_unlock(&bus->lock); return -EBUSY;
    }
    adu->unit_id = equipment->devices[d].unit_id;
    uint8_t fc = adu->fc;
    uint16_t address = adu->length >= 4 ? sys_get_be16(adu->data) : 0;
    uint16_t value = adu->length >= 4 ? sys_get_be16(adu->data + 2) : 0;
    int err = bus->cfg.transfer ? bus->cfg.transfer(bus->cfg.user, dev_id, adu) :
                                  co_modbus_transfer(bus->cfg.iface, dev_id, adu);
    if (!err && adu->unit_id != equipment->devices[d].unit_id) { err = -EBADMSG; }
    if (!err && adu->fc == (fc | 0x80) && adu->length == 1 && adu->data[0]) { err = adu->data[0]; }
    else if (!err && (adu->fc != fc || adu->unit_id != equipment->devices[d].unit_id ||
                     adu->length > sizeof(adu->data))) { err = -EBADMSG; }
    if (!err && (fc == 3 || fc == 4) &&
        (adu->length != 1U + value * 2U || adu->data[0] != value * 2U)) { err = -EBADMSG; }
    if (!err && (fc == 6 || fc == 16) &&
        (adu->length != 4 || sys_get_be16(adu->data) != address ||
         sys_get_be16(adu->data + 2) != value)) { err = -EBADMSG; }
    k_mutex_unlock(&bus->lock);
    outcome->replied = err >= 0;
    outcome->missing = err == -ETIMEDOUT || err == -EBADMSG;
    return err;
}

int co_transfer_mode(uint16_t dev_id, enum co_port_mode mode, struct modbus_adu *adu)
{
    struct co_outcome outcome;
    int err = exchange(dev_id, mode, adu, &outcome);
    co_state_record(dev_id, err, outcome);
    return err;
}

int co_transfer(uint16_t dev_id, struct modbus_adu *adu)
{
    return co_transfer_mode(dev_id, CO_PORT_POLL, adu);
}

static int read_registers(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count,
                           struct co_outcome *outcome, bool record)
{
    if (!words || !count || count > CO_MAX_REGS || 1 + count * 2 > sizeof(((struct modbus_adu *)0)->data) || (fc != 3 && fc != 4) ||
        (uint32_t)address + count > 65536) { return -EINVAL; }
    struct modbus_adu adu = {.fc = fc, .length = 4};
    sys_put_be16(address, adu.data); sys_put_be16(count, adu.data + 2);
    int err = exchange(dev_id, CO_PORT_POLL, &adu, outcome);
    if (!err) {
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu.data + 1 + 2 * i); }
    }
    if (record) { co_state_record(dev_id, err, *outcome); }
    return err;
}

int co_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count)
{
    struct co_outcome outcome = {0};
    return read_registers(dev_id, fc, address, words, count, &outcome, true);
}

int co_write(uint16_t dev_id, uint16_t address, const uint16_t *words, size_t count)
{
    if (!words || !count || count > CO_MAX_REGS || 5 + count * 2 > sizeof(((struct modbus_adu *)0)->data) || (uint32_t)address + count > 65536) { return -EINVAL; }
    struct modbus_adu adu = {.fc = count == 1 ? 6 : 16, .length = count == 1 ? 4 : 5 + count * 2};
    sys_put_be16(address, adu.data);
    sys_put_be16(count == 1 ? words[0] : count, adu.data + 2);
    if (count > 1) {
        adu.data[4] = count * 2;
        for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], adu.data + 5 + i * 2); }
    }
    struct co_outcome outcome;
    int err = exchange(dev_id, CO_PORT_POLL, &adu, &outcome);
    co_state_record(dev_id, err, outcome);
    return err;
}

#if defined(CONFIG_DP_PARAMS)
static int parameter_read(void *user, uint16_t dev_id, uint16_t address,
                          uint16_t *words, size_t count)
{
    ARG_UNUSED(user);
    return co_read(dev_id, 3, address, words, count);
}

static int parameter_write(void *user, uint16_t dev_id, uint16_t address,
                           const uint16_t *words, size_t count)
{
    ARG_UNUSED(user);
    return co_write(dev_id, address, words, count);
}

static bool parameter_simulated(void *user, uint16_t dev_id)
{
    ARG_UNUSED(user);
    struct co_device_status status;
    return !co_device_status(dev_id, &status) && status.simulated;
}
#endif

const struct dp_params_transport *co_parameter_transport(void)
{
#if defined(CONFIG_DP_PARAMS)
    static const struct dp_params_transport transport = {
        .read = parameter_read, .write = parameter_write,
        .simulated = parameter_simulated, .user = NULL,
    };
    return &transport;
#else
    return NULL;
#endif
}

int co_init_bus(const struct dp_equip_cfg *cfg, const struct co_bus_cfg *config)
{
    if (!config || config->id >= ARRAY_SIZE(buses) || (!config->transfer && !config->read_model && config->iface < 0) ||
        !cfg || cfg->device_count > CONFIG_DP_MAX_DEVICES) { return -EINVAL; }
    if (!IS_ENABLED(CONFIG_CO_MODBUS_ADAPTER) && !config->transfer && !config->read_model) {
        return -ENOTSUP;
    }
    int err = dp_validate(cfg);
    if (err) { return err; }
    /* Worker capacity belongs to collect; DP only validates point-table data. */
    for (size_t i = 0; i < cfg->device_count; ++i) {
        if (cfg->devices[i].bus_id >= ARRAY_SIZE(buses)) { return -EINVAL; }
    }
    for (size_t i = 0; i < cfg->collect_count; ++i) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(cfg, i, &scratch);
        if (!poll->model_protocol &&
            poll->count * 2 + 1 > sizeof(((struct modbus_adu *)0)->data)) {
            return -EMSGSIZE;
        }
    }
    if (started || buses[config->id].ready || (equipment && equipment != cfg)) { return -EALREADY; }
    for (size_t i = 0; i < ARRAY_SIZE(buses); ++i) {
        if (buses[i].ready && config->iface >= 0 && buses[i].cfg.iface == config->iface) { return -EEXIST; }
    }
    equipment = cfg;
    struct bus_state *bus = &buses[config->id];
    k_mutex_init(&bus->lock);
    k_sem_init(&bus->wake, 0, 1);
    bus->cfg = *config; bus->mode = CO_PORT_POLL; bus->ready = true;
    zbus_chan_set_msg_sub_pool(&co_device_events, &device_event_pool);
    co_state_bind(cfg, config->id, config->simulated);
    return 0;
}

int co_init(const struct dp_equip_cfg *cfg, int iface, bool simulated)
{
    const struct co_bus_cfg bus = {.id = 0, .iface = iface, .simulated = simulated};
    return co_init_bus(cfg, &bus);
}

static int collect_request(const struct co_cfg *cfg, bool sync, struct co_outcome *outcome)
{
    if (!data_callbacks.sample) { return -ENOTCONN; }
    struct bus_state *bus = bus_for(cfg->dev_id);
    if (!bus) { return -ENODEV; }
    uint16_t registers[CO_MAX_REGS];
    int err;
    if (cfg->model_protocol) {
        k_mutex_lock(&bus->lock, K_FOREVER);
        err = (bus->mode != CO_PORT_POLL &&
               !(bus->mode == CO_PORT_INIT && bus->init_owner == k_current_get())) ? -EBUSY :
              !bus->cfg.read_model ? -ENOTSUP :
              bus->cfg.read_model(bus->cfg.user, cfg->dev_id, cfg->model_id, cfg->address, registers, cfg->count);
        k_mutex_unlock(&bus->lock);
        *outcome = (struct co_outcome){.replied = err >= 0,
            .missing = err == -ETIMEDOUT || err == -EBADMSG};
    } else {
        err = read_registers(cfg->dev_id, cfg->function, cfg->address, registers, cfg->count, outcome, false);
    }
    struct dp_point_batch batch = {.source = DP_SOURCE_MODBUS,
        .flags = bus->cfg.simulated ? DP_QUALITY_SIMULATED : 0, .count = cfg->field_count};
    for (size_t i = 0; i < cfg->field_count; ++i) {
        const struct co_field *f = &cfg->fields[i];
        struct dp_point_update *p = &batch.points[i];
        p->key = (struct dp_key){cfg->dev_id, f->sig_id}; p->width = f->width; p->error = err;
        if (!err) {
            for (size_t w = 0; w < f->width; ++w) {
                p->value.words[w] = dp_order_word(registers + f->offset, f->width, w, f->order);
            }
        }
    }
    int publish = data_callbacks.sample(&batch, sync);
    int result = publish ? publish : err;
    co_state_record(cfg->dev_id, result, *outcome);
    return result;
}

static int collect_poll(size_t block, bool sync)
{
    if (!equipment || block >= equipment->collect_count) { return -EINVAL; }
    struct co_cfg scratch;
    struct co_outcome outcome = {0};
    return collect_request(dp_config_collect(equipment, block, &scratch), sync, &outcome);
}

/* Build a bounded request on the caller's stack; immutable rows stay in ROM.
 * Explicit co_poll keeps its one-row contract. Scheduled/initialization reads
 * combine adjacent compatible rows and report how many deadlines were served.
 */
static int collect_group(size_t first, bool sync, bool due_only, size_t *rows,
                          uint8_t retries, struct co_outcome *outcome)
{
    struct co_cfg scratch;
    struct co_cfg request = *dp_config_collect(equipment, first, &scratch);
    struct co_field fields[DP_BATCH_POINTS];
    memcpy(fields, request.fields, request.field_count * sizeof(*fields));
    request.fields = fields;
    *rows = 1;
    k_mutex_lock(&schedule_lock, K_FOREVER);
    int64_t now = k_uptime_get();
    for (size_t i = first + 1; !request.model_protocol && i < equipment->collect_count; ++i) {
        const struct co_cfg *next = dp_config_collect(equipment, i, &scratch);
        if (next->model_protocol || next->dev_id != request.dev_id ||
            next->function != request.function || next->init_only != request.init_only ||
            next->period_ms != request.period_ms || next->priority != request.priority ||
            (uint32_t)request.address + request.count != next->address ||
            request.count + next->count > CO_MAX_REGS ||
            1U + (request.count + next->count) * 2U > sizeof(((struct modbus_adu *)0)->data) ||
            request.field_count + next->field_count > ARRAY_SIZE(fields) ||
            (due_only && (disabled[i] || next_poll[i] > now))) { break; }
        for (size_t f = 0; f < next->field_count; ++f) {
            struct co_field *field = &fields[request.field_count++];
            *field = next->fields[f];
            field->offset += request.count;
        }
        request.count += next->count;
        ++*rows;
    }
    k_mutex_unlock(&schedule_lock);
    for (unsigned attempt = 0; ; ++attempt) {
        *outcome = (struct co_outcome){0};
        int err = collect_request(&request, sync, outcome);
        if (!outcome->missing || attempt >= retries) { return err; }
    }
}

int co_poll(size_t block) { return collect_poll(block, false); }

int co_set_mode(uint8_t id, enum co_port_mode mode)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || (unsigned)mode > CO_PORT_SLEEP) { return -EINVAL; }
    struct bus_state *bus = &buses[id];
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    bus->mode = mode;
    k_mutex_unlock(&schedule_lock);
    k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    return 0;
}

int co_get_mode(uint8_t id, enum co_port_mode *mode)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || !mode) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER); *mode = buses[id].mode;
    k_mutex_unlock(&schedule_lock); return 0;
}

int co_set_port_mode(uint16_t id, uint8_t mode, uint32_t timeout_ms)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || mode >= MODE_OTHER) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    struct bus_state *bus = &buses[id];
    k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(timeout_ms));
    int busy = timeout_ms ? -ETIMEDOUT : -EBUSY;
    if (k_mutex_lock(&bus->lock, sys_timepoint_timeout(deadline))) { return busy; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    if (k_mutex_lock(&schedule_lock, sys_timepoint_timeout(deadline))) {
        k_mutex_unlock(&bus->lock); return busy;
    }
    /* The supported reference modes have the same numbers; OTHER does not. */
    bus->mode = (enum co_port_mode)mode;
    k_mutex_unlock(&schedule_lock);
    k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    return 0;
}

int co_get_port_mode(uint16_t id, uint8_t *mode)
{
    if (id >= ARRAY_SIZE(buses) || !mode) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    enum co_port_mode current;
    int err = co_get_mode((uint8_t)id, &current);
    /* Native SLEEP cannot be reported as reference OTHER. */
    if (!err && current == CO_PORT_SLEEP) { return -ENOTSUP; }
    if (!err) { *mode = (uint8_t)current; }
    return err;
}

int co_dev_init(uint16_t dev_id)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    return co_initialize_device(dev_id);
}

int co_clean_set_queue(CO_CLEAN_QUEUE_ST *queue)
{
    if (!queue || queue->port_id >= ARRAY_SIZE(buses)) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    return co_cancel((uint8_t)queue->port_id);
}

int co_set_scan_pkg_flag(uint8_t dev_id, uint16_t address_model,
                         uint16_t function_offset, uint8_t flag)
{
    if (flag != CO_SCAN_PKG_ON && flag != CO_SCAN_PKG_OFF) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    int d = device_index(dev_id);
    if (d < 0) { return d; }
    bool found = false, model = false, matched = false;
    /* Resolve protocol before touching any schedule state. Otherwise an RTU
     * function could accidentally match an unrelated model offset. */
    for (size_t i = 0; i < equipment->collect_count; ++i) {
        struct co_cfg p_scratch;
        const struct co_cfg *p = dp_config_collect(equipment, i, &p_scratch);
        if (p->dev_id != dev_id) { continue; }
        if (found && model != p->model_protocol) { return -EPROTOTYPE; }
        found = true; model = p->model_protocol;
    }
    if (!found) { return -ENOENT; }
    if (!model && function_offset != 3 && function_offset != 4) { return -EINVAL; }
    struct bus_state *bus = bus_for(dev_id);
    if (!bus) { return -ENODEV; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    for (size_t i = 0; i < equipment->collect_count; ++i) {
        struct co_cfg p_scratch;
        const struct co_cfg *p = dp_config_collect(equipment, i, &p_scratch);
        if (p->dev_id != dev_id ||
            (model ? p->model_id != address_model || p->address != function_offset :
                     p->address != address_model || p->function != function_offset)) { continue; }
        disabled[i] = flag == CO_SCAN_PKG_OFF;
        next_poll[i] = k_uptime_get();
        matched = true;
    }
    k_mutex_unlock(&schedule_lock);
    if (matched) { k_sem_give(&bus->wake); }
    return matched ? 0 : -ENOENT;
}

int co_enable_poll(size_t block, bool enabled)
{
    if (!equipment || block >= equipment->collect_count) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    disabled[block] = !enabled; next_poll[block] = k_uptime_get();
    k_mutex_unlock(&schedule_lock);
    struct co_cfg scratch;
    const struct co_cfg *poll = dp_config_collect(equipment, block, &scratch);
    struct bus_state *bus = bus_for(poll->dev_id);
    if (bus) { k_sem_give(&bus->wake); }
    return 0;
}

int co_device_status(uint16_t dev_id, struct co_device_status *status)
{
    return co_state_get(dev_id, status);
}

int co_initialize_device(uint16_t dev_id)
{
    struct bus_state *bus = bus_for(dev_id);
    if (!bus) { return -ENODEV; }
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    enum co_port_mode old = bus->mode; bus->mode = CO_PORT_INIT;
    bus->init_owner = k_current_get();
    k_mutex_unlock(&schedule_lock);
    k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    k_spinlock_key_t callback_key = k_spin_lock(&reference_callbacks_lock);
    CO_PORT_PF_PROC initialize = init_callbacks[bus->cfg.id];
    k_spin_unlock(&reference_callbacks_lock, callback_key);
    int err = initialize ? initialize(bus->cfg.id) : 0;
    if (!err && bus->cfg.initialize) { err = bus->cfg.initialize(bus->cfg.user, dev_id); }
    for (size_t i = 0; !err && i < equipment->collect_count; ++i) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(equipment, i, &scratch);
        if (poll->dev_id == dev_id && poll->init_only) {
            size_t rows;
            struct co_schedule schedule;
            struct co_outcome outcome;
            co_state_schedule(dev_id, &schedule);
            err = collect_group(i, true, false, &rows, schedule.retries, &outcome);
            i += rows - 1;
        }
    }
    if (!err && data_callbacks.initialize) { err = data_callbacks.initialize(dev_id); }
    co_state_initialized(dev_id, err);
    k_mutex_lock(&bus->lock, K_FOREVER);
    k_mutex_lock(&schedule_lock, K_FOREVER);
    bus->init_owner = NULL; bus->mode = old;
    k_mutex_unlock(&schedule_lock); k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    return err;
}

int co_submit(const struct co_request *request, co_done_fn done, void *user)
{
    struct bus_state *bus = request ? bus_for(request->dev_id) : NULL;
    if (!bus || !done || request->priority > 3 || (unsigned)request->mode >= CO_PORT_SLEEP || k_is_in_isr() || request->adu.length > sizeof(request->adu.data)) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    int err = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(bus->queue); ++i) {
        if (!bus->queue[i].used) {
            bus->queue[i] = (struct queued_request){.request = *request, .done = done,
                .user = user, .queued_at = k_uptime_get(), .used = true};
            err = 0; break;
        }
    }
    k_mutex_unlock(&schedule_lock);
    if (!err) { k_sem_give(&bus->wake); }
    return err;
}

int co_cancel(uint8_t id)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready) { return -EINVAL; }
    for (size_t i = 0; i < ARRAY_SIZE(buses[id].queue); ++i) {
        k_mutex_lock(&schedule_lock, K_FOREVER);
        struct queued_request job = buses[id].queue[i]; buses[id].queue[i].used = false;
        k_mutex_unlock(&schedule_lock);
        if (job.used) { job.done(&job.request, -ECANCELED, job.user); }
    }
    k_sem_give(&buses[id].wake);
    return 0;
}

int co_reconfigure(uint8_t id, struct modbus_iface_param param)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || buses[id].cfg.transfer || buses[id].cfg.iface < 0) { return -EINVAL; }
    struct bus_state *bus = &buses[id];
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    int err = co_modbus_reconfigure(bus->cfg.iface, param);
    if (err) {
        k_mutex_lock(&schedule_lock, K_FOREVER); bus->mode = CO_PORT_SLEEP;
        k_mutex_unlock(&schedule_lock);
    }
    k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    return err;
}

void co_bus_task(void *arg, void *b, void *c)
{
    uintptr_t id = (uintptr_t)arg;
    ARG_UNUSED(b); ARG_UNUSED(c);
    if (!started || id >= ARRAY_SIZE(buses) || !buses[id].ready) { return; }
    struct bus_state *bus = &buses[id];
    k_mutex_lock(&schedule_lock, K_FOREVER);
    if (bus->running) { k_mutex_unlock(&schedule_lock); return; }
    bus->running = true;
    k_mutex_unlock(&schedule_lock);
    while (true) {
        co_state_tick();
        int poll = -1, queued = -1; int64_t best = INT64_MIN, now = k_uptime_get();
        int64_t deadline = INT64_MAX;
        struct queued_request job;
        k_mutex_lock(&schedule_lock, K_FOREVER);
        if (bus->mode != CO_PORT_SLEEP && !bus->init_owner) {
            for (size_t i = 0; i < ARRAY_SIZE(bus->queue); ++i) {
                struct queued_request *q = &bus->queue[i];
                /* Priority advantage is bounded: one second of age per level. */
                int64_t score = now - q->queued_at + q->request.priority * 1000;
                if (q->used && q->request.mode == bus->mode && score > best) { best = score; queued = i; }
            }
        }
        if (bus->mode == CO_PORT_POLL && !bus->init_owner) {
            for (size_t i = 0; i < equipment->collect_count; ++i) {
                struct co_cfg p_scratch;
                const struct co_cfg *p = dp_config_collect(equipment, i, &p_scratch);
                if (bus_for(p->dev_id) != bus) { continue; }
                struct co_schedule schedule;
                co_state_schedule(p->dev_id, &schedule);
                int64_t due = next_poll[i];
                if (schedule.managed && (schedule.state == CO_DEVICE_INITIALIZING ||
                                          schedule.state == CO_DEVICE_OFFLINE)) {
                    if (i != schedule.cursor) { continue; }
                    due = schedule.due;
                } else {
                    if (schedule.managed && schedule.state == CO_DEVICE_EXCEPTION) {
                        deadline = MIN(deadline, schedule.due);
                    }
                    if (disabled[i] || p->init_only) { continue; }
                }
                if (now < due) { deadline = MIN(deadline, due); continue; }
                int64_t score = now - due + p->priority * 1000;
                if (score > best) { best = score; poll = i; queued = -1; }
            }
        }
        if (queued >= 0) { job = bus->queue[queued]; bus->queue[queued].used = false; }
        k_mutex_unlock(&schedule_lock);
        if (queued >= 0) {
            int err = co_transfer_mode(job.request.dev_id, job.request.mode, &job.request.adu);
            job.done(&job.request, err, job.user);
        } else if (poll >= 0) {
            struct co_cfg scratch;
            const struct co_cfg *cfg = dp_config_collect(equipment, poll, &scratch);
            uint16_t dev = cfg->dev_id;
            uint32_t period = cfg->period_ms;
            struct co_schedule before;
            co_state_schedule(dev, &before);
            bool initializing = before.managed &&
                (before.state == CO_DEVICE_INITIALIZING || before.state == CO_DEVICE_OFFLINE);
            struct co_outcome outcome = {0};
            size_t rows = 1;
            k_mutex_lock(&bus->lock, K_FOREVER);
            bool running = bus->mode == CO_PORT_POLL;
            k_mutex_unlock(&bus->lock);
            /* exchange() serializes each physical transaction. Never retain
             * the bus lock while waiting for DP SAMPLE/business completion. */
            int err = running ? collect_group(poll, initializing, !initializing, &rows,
                                              before.retries, &outcome) : -EBUSY;
            if (!running) { continue; }
            co_state_complete(dev, &before, poll, rows, err, outcome);
            struct co_schedule after;
            co_state_schedule(dev, &after);
            k_mutex_lock(&schedule_lock, K_FOREVER);
            int64_t next = k_uptime_get() + period;
            for (size_t i = 0; i < rows; ++i) { next_poll[poll + i] = next; }
            if (initializing && after.state == CO_DEVICE_ONLINE) {
                for (size_t i = 0; i < equipment->collect_count; ++i) {
                    const struct co_cfg *p = dp_config_collect(equipment, i, &scratch);
                    if (p->dev_id == dev) { next_poll[i] = k_uptime_get(); }
                }
            }
            k_mutex_unlock(&schedule_lock);
        } else {
            /* Updates give a latched semaphore after changing the schedule.
             * A wake racing this scan is retained, including before take().
             * Sleeping/owned buses and queues for other modes have no deadline.
             */
            k_timeout_t timeout = deadline == INT64_MAX ? K_FOREVER :
                                  K_MSEC(MAX(deadline - k_uptime_get(), 0));
            k_sem_take(&bus->wake, timeout);
        }
    }
}

int co_polling_init(void)
{
    if (!equipment) { return -EACCES; }
    if (started) { return -EALREADY; }
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (!bus_for(equipment->devices[i].dev_id)) { return -ENODEV; }
    }
    co_state_arm();
    started = true;
    return 0;
}

int co_init_func_reg(uint16_t port, CO_PORT_PF_PROC callback)
{
    if (port >= ARRAY_SIZE(init_callbacks) || k_is_in_isr()) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&reference_callbacks_lock);
    init_callbacks[port] = callback;
    k_spin_unlock(&reference_callbacks_lock, key);
    return 0;
}

int co_set_value_trans_cb_reg(CO_SET_VALUE_TRANS_CB callback)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_spinlock_key_t key = k_spin_lock(&reference_callbacks_lock);
    value_transform = callback;
    k_spin_unlock(&reference_callbacks_lock, key);
    return 0;
}

struct reference_write {
    uint16_t dev, address, count;
    uint16_t words[MIN(CO_MAX_REGS, DP_POINT_WORDS)];
};
static struct reference_write reference_writes[CONFIG_DP_PROCESS_MAX_OUTPUTS];
K_MUTEX_DEFINE(reference_write_lock);

int co_dp_fast_set(PARA_DATA_S *data, PARA_SET_STATUS status)
{
    if (!data || !data->p_set_data || !data->set_num || data->set_num > ARRAY_SIZE(reference_writes) ||
        k_is_in_isr()) { return -EINVAL; }
    if (data->type > DP_NO_DM_SET) { return -ENOTSUP; }
    if (k_mutex_lock(&reference_write_lock, K_NO_WAIT)) { return -EBUSY; }
    k_spinlock_key_t key = k_spin_lock(&reference_callbacks_lock);
    CO_SET_VALUE_TRANS_CB transform = value_transform;
    k_spin_unlock(&reference_callbacks_lock, key);
    int err = 0;
    for (size_t i = 0; i < data->set_num; ++i) {
        DATA_SET_S *item = &data->p_set_data[i];
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find((struct dp_key){item->dev_id, item->sig_id}, &p_scratch);
        const uint8_t *bytes = item->sig_size > 4 ? item->p_value.pst : item->p_value.str;
        if (!p || !bytes || item->sig_size != dp_reference_size(p) || p->width > CO_MAX_REGS ||
            item->extra.addr_type != FUNC_PARA_REG_TYPE_MB || !bus_for(item->dev_id)) {
            err = -EINVAL; break;
        }
        if (p->source == DP_SOURCE_CONSTANT) { err = -EACCES; break; }
        struct reference_write *write = &reference_writes[i];
        enum dp_word_order order = p->write_order;
        uint16_t address = p->write_address;
        bool mapped = p->source == DP_SOURCE_PARAMETER && (p->param_flags & DP_PARAM_FORWARD);
        if (!mapped) {
            for (size_t b = 0; b < equipment->collect_count && !mapped; ++b) {
                struct co_cfg poll_scratch;
                const struct co_cfg *poll = dp_config_collect(equipment, b, &poll_scratch);
                if (poll->dev_id != item->dev_id || poll->model_protocol || poll->function != 3) { continue; }
                for (size_t f = 0; f < poll->field_count; ++f) {
                    if (poll->fields[f].sig_id == item->sig_id && poll->fields[f].width == p->width) {
                        address = poll->address + poll->fields[f].offset;
                        order = poll->fields[f].order; mapped = true; break;
                    }
                }
            }
        }
        if (!mapped || (uint32_t)address + p->width > 65536) { err = -ENOTSUP; break; }
        CO_SET_VALUE_TRANS_S converted = {.dev_id = item->dev_id, .sig_id = item->sig_id,
                                         .sig_size = item->sig_size};
        if (transform) {
            if (item->sig_size > sizeof(converted.trans_val)) { err = -EMSGSIZE; break; }
            memcpy(converted.trans_val, bytes, item->sig_size);
            if (!transform(&converted)) {
                if (converted.sig_size != item->sig_size || converted.dev_id != item->dev_id ||
                    converted.sig_id != item->sig_id) { err = -EINVAL; break; }
                bytes = converted.trans_val;
            }
        }
        struct dp_value value;
        dp_reference_unpack(&value, bytes, item->sig_size);
        err = dp_check_value(p, &value);
        if (err) { break; }
        *write = (struct reference_write){.dev = item->dev_id, .address = address, .count = p->width};
        for (size_t w = 0; w < p->width; ++w) { write->words[w] = dp_order_word(value.words, p->width, w, order); }
    }
    for (size_t i = 0; !err && i < data->set_num; ++i) {
        const struct reference_write *w = &reference_writes[i];
        err = co_write(w->dev, w->address, w->words, w->count);
        if (err > 0) { err = -EIO; }
    }
    k_mutex_unlock(&reference_write_lock);
    if (!err && status) { status(data); }
    return err;
}

int co_anlysis_mb_pkg(uint8_t dev, uint16_t address, uint8_t *frame, uint16_t length)
{
    struct bus_state *bus = bus_for(dev);
    int device = device_index(dev);
    if (!bus || !frame || device < 0 || length < 5 || length > 256 || k_is_in_isr()) { return -EINVAL; }
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= frame[i];
        for (unsigned b = 0; b < 8; ++b) { crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0); }
    }
    if (crc || frame[0] != equipment->devices[device].unit_id) { return -EBADMSG; }
    if ((frame[1] == 0x83 || frame[1] == 0x84) && length == 5 && frame[2]) {
        co_state_record(dev, frame[2], (struct co_outcome){.replied = true}); return -EIO;
    }
    if ((frame[1] != 3 && frame[1] != 4) || !frame[2] || frame[2] % 2 ||
        length != 5U + frame[2] || (uint32_t)address + frame[2] / 2 > 65536) { return -EBADMSG; }
    struct dp_point_batch batch = {.source = DP_SOURCE_MODBUS,
        .flags = bus->cfg.simulated ? DP_QUALITY_SIMULATED : 0};
    for (size_t b = 0; b < equipment->collect_count; ++b) {
        struct co_cfg poll_scratch;
        const struct co_cfg *poll = dp_config_collect(equipment, b, &poll_scratch);
        if (poll->dev_id != dev || poll->model_protocol || poll->function != frame[1]) { continue; }
        for (size_t f = 0; f < poll->field_count; ++f) {
            const struct co_field *field = &poll->fields[f];
            uint32_t begin = poll->address + field->offset;
            if (begin < address || begin + field->width > (uint32_t)address + frame[2] / 2) { continue; }
            if (batch.count == ARRAY_SIZE(batch.points)) { return -ENOSPC; }
            for (size_t i = 0; i < batch.count; ++i) {
                if (batch.points[i].key.sig_id == field->sig_id) { return -EEXIST; }
            }
            struct dp_point_update *point = &batch.points[batch.count++];
            point->key = (struct dp_key){dev, field->sig_id}; point->width = field->width;
            uint16_t words[DP_POINT_WORDS];
            for (size_t w = 0; w < field->width; ++w) { words[w] = sys_get_be16(frame + 3 + 2 * (begin - address + w)); }
            for (size_t w = 0; w < field->width; ++w) { point->value.words[w] = dp_order_word(words, field->width, w, field->order); }
        }
    }
    if (!batch.count) { return -ENOENT; }
    int err = data_callbacks.sample ? data_callbacks.sample(&batch, false) : -ENOTCONN;
    if (!err) { co_state_record(dev, 0, (struct co_outcome){.replied = true}); }
    return err;
}

void co_set_reboot_system(void)
{
    if (dp_reference_context()) { dp_set_reboot(); }
    else { dp_reboot_system(); }
}
