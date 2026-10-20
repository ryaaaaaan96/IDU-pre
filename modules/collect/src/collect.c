#include <collect/collect.h>
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
    struct k_thread thread;
    struct queued_request queue[CONFIG_CO_QUEUE_DEPTH];
};
static struct bus_state buses[CONFIG_DP_MAX_BUSES];
K_THREAD_STACK_ARRAY_DEFINE(bus_stacks, CONFIG_DP_MAX_BUSES, CONFIG_CO_STACK_SIZE);
static const struct dp_equip_cfg *equipment;
static struct co_device_status statuses[CONFIG_DP_MAX_DEVICES];
static int64_t next_poll[CONFIG_DP_POINTS_MAX_POLLS];
static bool disabled[CONFIG_DP_POINTS_MAX_POLLS];
static bool started;
static CO_PORT_PF_PROC init_callbacks[CONFIG_DP_MAX_BUSES];
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

static void record_status(uint16_t dev_id, int err)
{
    int i = device_index(dev_id);
    if (i < 0) { return; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    struct co_device_status *s = &statuses[i];
    s->dev_id = dev_id;
    s->bus_id = equipment->devices[i].bus_id;
    s->simulated = buses[s->bus_id].cfg.simulated;
    s->error = err;
    s->state = !err ? CO_DEVICE_ONLINE : err > 0 ? CO_DEVICE_EXCEPTION : CO_DEVICE_OFFLINE;
    if (!err) { ++s->successes; s->last_success = k_uptime_get(); }
    else { ++s->failures; }
    struct co_device_status event = *s;
    k_mutex_unlock(&schedule_lock);
    zbus_chan_pub(&co_device_events, &event, K_NO_WAIT);
}

/* Standard functions also work on Zephyr RAW loopback client interfaces. */
static int standard_transfer(int iface, struct modbus_adu *adu)
{
    if (adu->length < 4 || adu->length > sizeof(adu->data)) { return -EINVAL; }
    uint16_t address = sys_get_be16(adu->data), count = sys_get_be16(adu->data + 2);
    uint16_t words[123];
    int err;
    switch (adu->fc) {
    case 3: case 4:
        if (adu->length != 4 || !count || count > 123 || 1 + count * 2 > sizeof(adu->data) || (uint32_t)address + count > 65536) { return -EINVAL; }
        err = adu->fc == 3 ? modbus_read_holding_regs(iface, adu->unit_id, address, words, count) :
                            modbus_read_input_regs(iface, adu->unit_id, address, words, count);
        if (!err) {
            adu->length = 1 + count * 2; adu->data[0] = count * 2;
            for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], adu->data + 1 + 2 * i); }
        }
        return err;
    case 6:
        if (adu->length != 4) { return -EINVAL; }
        return modbus_write_holding_reg(iface, adu->unit_id, address, count);
    case 16:
        if (!count || count > 123 || adu->length != 5 + count * 2 ||
            adu->data[4] != count * 2 || (uint32_t)address + count > 65536) { return -EINVAL; }
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu->data + 5 + i * 2); }
        err = modbus_write_holding_regs(iface, adu->unit_id, address, words, count);
        if (!err) { adu->length = 4; }
        return err;
    default:
#if defined(CONFIG_MODBUS_RAW_ADU)
        return modbus_raw_backend_txn(iface, adu);
#else
        return -ENOTSUP;
#endif
    }
}

int co_transfer_mode(uint16_t dev_id, enum co_port_mode mode, struct modbus_adu *adu)
{
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
    int err = bus->cfg.transfer ? bus->cfg.transfer(bus->cfg.user, adu) :
                                  standard_transfer(bus->cfg.iface, adu);
    if (!err && adu->unit_id != equipment->devices[d].unit_id) { err = -EBADMSG; }
    if (!err && adu->fc == (fc | 0x80) && adu->length == 1 && adu->data[0]) { err = adu->data[0]; }
    else if (!err && (adu->fc != fc || adu->unit_id != equipment->devices[d].unit_id ||
                     adu->length > sizeof(adu->data))) { err = -EBADMSG; }
    k_mutex_unlock(&bus->lock);
    record_status(dev_id, err);
    return err;
}

int co_transfer(uint16_t dev_id, struct modbus_adu *adu)
{
    return co_transfer_mode(dev_id, CO_PORT_POLL, adu);
}

int co_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count)
{
    if (!words || !count || count > CO_MAX_REGS || 1 + count * 2 > sizeof(((struct modbus_adu *)0)->data) || (fc != 3 && fc != 4) ||
        (uint32_t)address + count > 65536) { return -EINVAL; }
    struct modbus_adu adu = {.fc = fc, .length = 4};
    sys_put_be16(address, adu.data); sys_put_be16(count, adu.data + 2);
    int err = co_transfer(dev_id, &adu);
    if (!err && (adu.length != 1 + count * 2 || adu.data[0] != count * 2)) { err = -EBADMSG; record_status(dev_id, err); }
    if (!err) {
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu.data + 1 + 2 * i); }
    }
    return err;
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
    int err = co_transfer(dev_id, &adu);
    if (!err && (adu.length != 4 || sys_get_be16(adu.data) != address ||
        sys_get_be16(adu.data + 2) != (count == 1 ? words[0] : count))) { record_status(dev_id, -EBADMSG); return -EBADMSG; }
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

int co_init_bus(const struct dp_equip_cfg *cfg, const struct co_bus_cfg *config)
{
    if (!config || config->id >= ARRAY_SIZE(buses) || (!config->transfer && !config->read_model && config->iface < 0) ||
        !cfg || cfg->device_count > CONFIG_DP_MAX_DEVICES) { return -EINVAL; }
    int err = dp_validate(cfg);
    if (err) { return err; }
    for (size_t i = 0; i < cfg->collect_count; ++i) {
        if (!cfg->collect[i].model_protocol &&
            cfg->collect[i].count * 2 + 1 > sizeof(((struct modbus_adu *)0)->data)) {
            return -EMSGSIZE;
        }
    }
    if (started || buses[config->id].ready || (equipment && equipment != cfg)) { return -EALREADY; }
    for (size_t i = 0; i < ARRAY_SIZE(buses); ++i) {
        if (buses[i].ready && config->iface >= 0 && buses[i].cfg.iface == config->iface) { return -EEXIST; }
    }
#if defined(CONFIG_DP_PARAMS)
    const struct dp_params_transport transport = {
        .read = parameter_read, .write = parameter_write,
        .simulated = parameter_simulated,
    };
    err = dp_params_set_transport(&transport);
    if (err) { return err; }
#endif
    err = dp_para_reg_set_func(co_dp_fast_set);
    if (err) { return err; }
    equipment = cfg;
    struct bus_state *bus = &buses[config->id];
    k_mutex_init(&bus->lock);
    k_sem_init(&bus->wake, 0, 1);
    bus->cfg = *config; bus->mode = CO_PORT_POLL; bus->ready = true;
    zbus_chan_set_msg_sub_pool(&co_device_events, &device_event_pool);
    return 0;
}

int co_init(const struct dp_equip_cfg *cfg, int iface, bool simulated)
{
    const struct co_bus_cfg bus = {.id = 0, .iface = iface, .simulated = simulated};
    return co_init_bus(cfg, &bus);
}

static int collect_poll(size_t block, bool sync)
{
    if (!equipment || block >= equipment->collect_count) { return -EINVAL; }
    const struct co_cfg *cfg = &equipment->collect[block];
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
        record_status(cfg->dev_id, err);
    } else {
        err = co_read(cfg->dev_id, cfg->function, cfg->address, registers, cfg->count);
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
    int publish = dp_publish(&batch, K_MSEC(20));
    if (publish || err || !sync) { return publish ? publish : err; }
    err = dp_wait_processed(batch.ticket, K_SECONDS(1));
    for (size_t i = 0; !err && i < batch.count; ++i) {
        struct dp_point_state state;
        err = dp_read(batch.points[i].key, &state);
        if (!err && (state.quality & 0xff) != DP_QUALITY_GOOD) { err = state.error ? state.error : -EIO; }
    }
    return err;
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
        const struct co_cfg *p = &equipment->collect[i];
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
        const struct co_cfg *p = &equipment->collect[i];
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
    struct bus_state *bus = bus_for(equipment->collect[block].dev_id);
    if (bus) { k_sem_give(&bus->wake); }
    return 0;
}

int co_device_status(uint16_t dev_id, struct co_device_status *status)
{
    int i = device_index(dev_id);
    if (i < 0 || !status) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER); *status = statuses[i]; status->dev_id = dev_id;
    status->bus_id = equipment->devices[i].bus_id;
    status->simulated = buses[status->bus_id].cfg.simulated;
    k_mutex_unlock(&schedule_lock); return 0;
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
        if (equipment->collect[i].dev_id == dev_id && equipment->collect[i].init_only) { err = collect_poll(i, true); }
    }
#if defined(CONFIG_DP_PROCESS)
    if (!err) { err = dp_process_init_device(dev_id); }
#endif
#if defined(CONFIG_DP_PARAMS)
    if (!err) { err = dp_params_sync_device(dev_id); }
#endif
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
    int err = modbus_disable(bus->cfg.iface);
    if (!err) { err = modbus_init_client(bus->cfg.iface, param); }
    if (err) {
        k_mutex_lock(&schedule_lock, K_FOREVER); bus->mode = CO_PORT_SLEEP;
        k_mutex_unlock(&schedule_lock);
    }
    k_mutex_unlock(&bus->lock);
    k_sem_give(&bus->wake);
    return err;
}

static void collect_thread(void *arg, void *b, void *c)
{
    struct bus_state *bus = arg; ARG_UNUSED(b); ARG_UNUSED(c);
    while (true) {
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
                const struct co_cfg *p = &equipment->collect[i];
                if (disabled[i] || p->init_only || bus_for(p->dev_id) != bus) { continue; }
                if (now < next_poll[i]) {
                    deadline = MIN(deadline, next_poll[i]);
                    continue;
                }
                int64_t score = now - next_poll[i] + p->priority * 1000;
                if (score > best) { best = score; poll = i; queued = -1; }
            }
        }
        if (queued >= 0) { job = bus->queue[queued]; bus->queue[queued].used = false; }
        k_mutex_unlock(&schedule_lock);
        if (queued >= 0) {
            int err = co_transfer_mode(job.request.dev_id, job.request.mode, &job.request.adu);
            job.done(&job.request, err, job.user);
        } else if (poll >= 0) {
            k_mutex_lock(&bus->lock, K_FOREVER);
            if (bus->mode == CO_PORT_POLL) { co_poll(poll); }
            k_mutex_unlock(&bus->lock);
            k_mutex_lock(&schedule_lock, K_FOREVER);
            next_poll[poll] = k_uptime_get() + equipment->collect[poll].period_ms;
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

int co_start(void)
{
    if (!equipment) { return -EACCES; }
    if (started) { return -EALREADY; }
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (!bus_for(equipment->devices[i].dev_id)) { return -ENODEV; }
    }
    started = true;
    for (size_t i = 0; i < ARRAY_SIZE(buses); ++i) {
        struct bus_state *bus = &buses[i];
        if (!bus->ready) { continue; }
        bus->running = true;
        k_thread_create(&bus->thread, bus_stacks[i], K_THREAD_STACK_SIZEOF(bus_stacks[i]),
                        collect_thread, bus, NULL, NULL, 7, 0, K_NO_WAIT);
        k_thread_name_set(&bus->thread, "dp_bus");
    }
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
        const struct dp_point_cfg *p = dp_find((struct dp_key){item->dev_id, item->sig_id});
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
                const struct co_cfg *poll = &equipment->collect[b];
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
        record_status(dev, frame[2]); return -EIO;
    }
    if ((frame[1] != 3 && frame[1] != 4) || !frame[2] || frame[2] % 2 ||
        length != 5U + frame[2] || (uint32_t)address + frame[2] / 2 > 65536) { return -EBADMSG; }
    struct dp_point_batch batch = {.source = DP_SOURCE_MODBUS,
        .flags = bus->cfg.simulated ? DP_QUALITY_SIMULATED : 0};
    for (size_t b = 0; b < equipment->collect_count; ++b) {
        const struct co_cfg *poll = &equipment->collect[b];
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
    int err = dp_publish(&batch, K_MSEC(20));
    if (!err) { record_status(dev, 0); }
    return err;
}

void co_set_reboot_system(void)
{
    if (dp_reference_context()) { dp_set_reboot(); }
    else { dp_reboot_system(); }
}
