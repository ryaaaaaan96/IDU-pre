#include <idu/collect.h>
#if defined(CONFIG_IDU_PARAMS)
#include <idu/params.h>
#include <idu/process.h>
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>

#define BUS_QUEUE 4
struct queued_request {
    struct idu_collect_request request;
    idu_collect_done_fn done;
    void *user;
    int64_t queued_at;
    bool used;
};
struct bus_state {
    struct idu_bus_cfg cfg;
    struct k_mutex lock;
    enum idu_port_mode mode;
    bool ready, running;
    k_tid_t init_owner;
    struct k_thread thread;
    struct queued_request queue[BUS_QUEUE];
};
static struct bus_state buses[CONFIG_IDU_BUS_COUNT];
K_THREAD_STACK_ARRAY_DEFINE(bus_stacks, CONFIG_IDU_BUS_COUNT, CONFIG_IDU_COLLECT_STACK_SIZE);
static const struct idu_equip_cfg *equipment;
static struct idu_device_status statuses[CONFIG_IDU_DEVICE_COUNT];
static int64_t next_poll[CONFIG_IDU_POINTS_MAX_POLLS];
static bool disabled[CONFIG_IDU_POINTS_MAX_POLLS];
static bool started;
K_MUTEX_DEFINE(schedule_lock);
NET_BUF_POOL_FIXED_DEFINE(device_event_pool, 2, sizeof(struct idu_device_status),
                         sizeof(struct zbus_channel *), NULL);
ZBUS_CHAN_DEFINE(idu_device_events, struct idu_device_status, NULL, NULL,
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
    struct idu_device_status *s = &statuses[i];
    s->dev_id = dev_id;
    s->bus_id = equipment->devices[i].bus_id;
    s->simulated = buses[s->bus_id].cfg.simulated;
    s->error = err;
    s->state = !err ? IDU_DEVICE_ONLINE : err > 0 ? IDU_DEVICE_EXCEPTION : IDU_DEVICE_OFFLINE;
    if (!err) { ++s->successes; s->last_success = k_uptime_get(); }
    else { ++s->failures; }
    struct idu_device_status event = *s;
    k_mutex_unlock(&schedule_lock);
    zbus_chan_pub(&idu_device_events, &event, K_NO_WAIT);
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

int idu_collect_transfer_mode(uint16_t dev_id, enum idu_port_mode mode, struct modbus_adu *adu)
{
    struct bus_state *bus = bus_for(dev_id);
    int d = device_index(dev_id);
    if (!bus || !adu || (unsigned)mode >= IDU_PORT_SLEEP || adu->length > sizeof(adu->data)) { return -EINVAL; }
    if (k_current_get() == &k_sys_work_q.thread || k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&bus->lock, K_FOREVER);
    if ((bus->mode != mode && !(bus->mode == IDU_PORT_INIT && bus->init_owner == k_current_get())) ||
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

int idu_collect_transfer(uint16_t dev_id, struct modbus_adu *adu)
{
    return idu_collect_transfer_mode(dev_id, IDU_PORT_POLL, adu);
}

int idu_collect_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count)
{
    if (!words || !count || count > IDU_POLL_REGS || 1 + count * 2 > sizeof(((struct modbus_adu *)0)->data) || (fc != 3 && fc != 4) ||
        (uint32_t)address + count > 65536) { return -EINVAL; }
    struct modbus_adu adu = {.fc = fc, .length = 4};
    sys_put_be16(address, adu.data); sys_put_be16(count, adu.data + 2);
    int err = idu_collect_transfer(dev_id, &adu);
    if (!err && (adu.length != 1 + count * 2 || adu.data[0] != count * 2)) { err = -EBADMSG; record_status(dev_id, err); }
    if (!err) {
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu.data + 1 + 2 * i); }
    }
    return err;
}

int idu_collect_write(uint16_t dev_id, uint16_t address, const uint16_t *words, size_t count)
{
    if (!words || !count || count > IDU_POLL_REGS || 5 + count * 2 > sizeof(((struct modbus_adu *)0)->data) || (uint32_t)address + count > 65536) { return -EINVAL; }
    struct modbus_adu adu = {.fc = count == 1 ? 6 : 16, .length = count == 1 ? 4 : 5 + count * 2};
    sys_put_be16(address, adu.data);
    sys_put_be16(count == 1 ? words[0] : count, adu.data + 2);
    if (count > 1) {
        adu.data[4] = count * 2;
        for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], adu.data + 5 + i * 2); }
    }
    int err = idu_collect_transfer(dev_id, &adu);
    if (!err && (adu.length != 4 || sys_get_be16(adu.data) != address ||
        sys_get_be16(adu.data + 2) != (count == 1 ? words[0] : count))) { record_status(dev_id, -EBADMSG); return -EBADMSG; }
    return err;
}

int idu_collect_init_bus(const struct idu_equip_cfg *cfg, const struct idu_bus_cfg *config)
{
    if (!config || config->id >= ARRAY_SIZE(buses) || (!config->transfer && !config->read_model && config->iface < 0) ||
        !cfg || cfg->device_count > CONFIG_IDU_DEVICE_COUNT) { return -EINVAL; }
    int err = idu_points_validate(cfg);
    if (err) { return err; }
    if (started || buses[config->id].ready || (equipment && equipment != cfg)) { return -EALREADY; }
    for (size_t i = 0; i < ARRAY_SIZE(buses); ++i) {
        if (buses[i].ready && config->iface >= 0 && buses[i].cfg.iface == config->iface) { return -EEXIST; }
    }
    equipment = cfg;
    struct bus_state *bus = &buses[config->id];
    k_mutex_init(&bus->lock);
    bus->cfg = *config; bus->mode = IDU_PORT_POLL; bus->ready = true;
    zbus_chan_set_msg_sub_pool(&idu_device_events, &device_event_pool);
    return 0;
}

int idu_collect_init(const struct idu_equip_cfg *cfg, int iface, bool simulated)
{
    const struct idu_bus_cfg bus = {.id = 0, .iface = iface, .simulated = simulated};
    return idu_collect_init_bus(cfg, &bus);
}

static int collect_poll(size_t block, bool sync)
{
    if (!equipment || block >= equipment->collect_count) { return -EINVAL; }
    const struct idu_collect_cfg *cfg = &equipment->collect[block];
    struct bus_state *bus = bus_for(cfg->dev_id);
    if (!bus) { return -ENODEV; }
    uint16_t registers[IDU_POLL_REGS];
    int err;
    if (cfg->model_protocol) {
        k_mutex_lock(&bus->lock, K_FOREVER);
        err = (bus->mode != IDU_PORT_POLL &&
               !(bus->mode == IDU_PORT_INIT && bus->init_owner == k_current_get())) ? -EBUSY :
              !bus->cfg.read_model ? -ENOTSUP :
              bus->cfg.read_model(bus->cfg.user, cfg->dev_id, cfg->model_id, cfg->address, registers, cfg->count);
        k_mutex_unlock(&bus->lock);
        record_status(cfg->dev_id, err);
    } else {
        err = idu_collect_read(cfg->dev_id, cfg->function, cfg->address, registers, cfg->count);
    }
    struct idu_point_batch batch = {.source = IDU_SOURCE_MODBUS,
        .flags = bus->cfg.simulated ? IDU_QUALITY_SIMULATED : 0, .count = cfg->field_count};
    for (size_t i = 0; i < cfg->field_count; ++i) {
        const struct idu_collect_field *f = &cfg->fields[i];
        struct idu_point_update *p = &batch.points[i];
        p->key = (struct idu_key){cfg->dev_id, f->sig_id}; p->width = f->width; p->error = err;
        if (!err) {
            for (size_t w = 0; w < f->width; ++w) {
                p->value.words[w] = idu_order_word(registers + f->offset, f->width, w, f->order);
            }
        }
    }
    int publish = idu_points_publish(&batch, K_MSEC(20));
    if (publish || err || !sync) { return publish ? publish : err; }
    err = idu_points_wait_processed(batch.ticket, K_SECONDS(1));
    for (size_t i = 0; !err && i < batch.count; ++i) {
        struct idu_point_state state;
        err = idu_points_read(batch.points[i].key, &state);
        if (!err && (state.quality & 0xff) != IDU_QUALITY_GOOD) { err = state.error ? state.error : -EIO; }
    }
    return err;
}

int idu_collect_poll(size_t block) { return collect_poll(block, false); }

int idu_collect_set_mode(uint8_t id, enum idu_port_mode mode)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || (unsigned)mode > IDU_PORT_SLEEP) { return -EINVAL; }
    struct bus_state *bus = &buses[id];
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    bus->mode = mode;
    k_mutex_unlock(&schedule_lock);
    k_mutex_unlock(&bus->lock);
    return 0;
}

int idu_collect_get_mode(uint8_t id, enum idu_port_mode *mode)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || !mode) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER); *mode = buses[id].mode;
    k_mutex_unlock(&schedule_lock); return 0;
}

int idu_collect_enable_poll(size_t block, bool enabled)
{
    if (!equipment || block >= equipment->collect_count) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    disabled[block] = !enabled; next_poll[block] = k_uptime_get();
    k_mutex_unlock(&schedule_lock); return 0;
}

int idu_collect_device_status(uint16_t dev_id, struct idu_device_status *status)
{
    int i = device_index(dev_id);
    if (i < 0 || !status) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER); *status = statuses[i]; status->dev_id = dev_id;
    status->bus_id = equipment->devices[i].bus_id;
    status->simulated = buses[status->bus_id].cfg.simulated;
    k_mutex_unlock(&schedule_lock); return 0;
}

int idu_collect_initialize_device(uint16_t dev_id)
{
    struct bus_state *bus = bus_for(dev_id);
    if (!bus) { return -ENODEV; }
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    enum idu_port_mode old = bus->mode; bus->mode = IDU_PORT_INIT;
    bus->init_owner = k_current_get();
    k_mutex_unlock(&schedule_lock);
    k_mutex_unlock(&bus->lock);
    int err = bus->cfg.initialize ? bus->cfg.initialize(bus->cfg.user, dev_id) : 0;
    for (size_t i = 0; !err && i < equipment->collect_count; ++i) {
        if (equipment->collect[i].dev_id == dev_id && equipment->collect[i].init_only) { err = collect_poll(i, true); }
    }
#if defined(CONFIG_IDU_PARAMS)
    if (!err) { err = idu_process_init_device(dev_id); }
    if (!err) { err = idu_params_sync_device(dev_id); }
#endif
    k_mutex_lock(&bus->lock, K_FOREVER);
    k_mutex_lock(&schedule_lock, K_FOREVER);
    bus->init_owner = NULL; bus->mode = old;
    k_mutex_unlock(&schedule_lock); k_mutex_unlock(&bus->lock);
    return err;
}

int idu_collect_submit(const struct idu_collect_request *request, idu_collect_done_fn done, void *user)
{
    struct bus_state *bus = request ? bus_for(request->dev_id) : NULL;
    if (!bus || !done || request->priority > 3 || (unsigned)request->mode >= IDU_PORT_SLEEP || k_is_in_isr() || request->adu.length > sizeof(request->adu.data)) { return -EINVAL; }
    k_mutex_lock(&schedule_lock, K_FOREVER);
    int err = -ENOSPC;
    for (size_t i = 0; i < BUS_QUEUE; ++i) {
        if (!bus->queue[i].used) {
            bus->queue[i] = (struct queued_request){.request = *request, .done = done,
                .user = user, .queued_at = k_uptime_get(), .used = true};
            err = 0; break;
        }
    }
    k_mutex_unlock(&schedule_lock); return err;
}

int idu_collect_cancel(uint8_t id)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready) { return -EINVAL; }
    for (size_t i = 0; i < BUS_QUEUE; ++i) {
        k_mutex_lock(&schedule_lock, K_FOREVER);
        struct queued_request job = buses[id].queue[i]; buses[id].queue[i].used = false;
        k_mutex_unlock(&schedule_lock);
        if (job.used) { job.done(&job.request, -ECANCELED, job.user); }
    }
    return 0;
}

int idu_collect_reconfigure(uint8_t id, struct modbus_iface_param param)
{
    if (id >= ARRAY_SIZE(buses) || !buses[id].ready || buses[id].cfg.transfer || buses[id].cfg.iface < 0) { return -EINVAL; }
    struct bus_state *bus = &buses[id];
    if (k_mutex_lock(&bus->lock, K_NO_WAIT)) { return -EBUSY; }
    if (bus->init_owner) { k_mutex_unlock(&bus->lock); return -EBUSY; }
    int err = modbus_disable(bus->cfg.iface);
    if (!err) { err = modbus_init_client(bus->cfg.iface, param); }
    if (err) {
        k_mutex_lock(&schedule_lock, K_FOREVER); bus->mode = IDU_PORT_SLEEP;
        k_mutex_unlock(&schedule_lock);
    }
    k_mutex_unlock(&bus->lock); return err;
}

static void collect_thread(void *arg, void *b, void *c)
{
    struct bus_state *bus = arg; ARG_UNUSED(b); ARG_UNUSED(c);
    while (true) {
        int poll = -1, queued = -1; int64_t best = INT64_MIN, now = k_uptime_get();
        struct queued_request job;
        k_mutex_lock(&schedule_lock, K_FOREVER);
        if (bus->mode != IDU_PORT_SLEEP && !bus->init_owner) {
            for (size_t i = 0; i < BUS_QUEUE; ++i) {
                struct queued_request *q = &bus->queue[i];
                /* Priority advantage is bounded: one second of age per level. */
                int64_t score = now - q->queued_at + q->request.priority * 1000;
                if (q->used && q->request.mode == bus->mode && score > best) { best = score; queued = i; }
            }
        }
        if (bus->mode == IDU_PORT_POLL) {
            for (size_t i = 0; i < equipment->collect_count; ++i) {
                const struct idu_collect_cfg *p = &equipment->collect[i];
                if (disabled[i] || p->init_only || bus_for(p->dev_id) != bus || now < next_poll[i]) { continue; }
                int64_t score = now - next_poll[i] + p->priority * 1000;
                if (score > best) { best = score; poll = i; queued = -1; }
            }
        }
        if (queued >= 0) { job = bus->queue[queued]; bus->queue[queued].used = false; }
        k_mutex_unlock(&schedule_lock);
        if (queued >= 0) {
            int err = idu_collect_transfer_mode(job.request.dev_id, job.request.mode, &job.request.adu);
            job.done(&job.request, err, job.user);
        } else if (poll >= 0) {
            k_mutex_lock(&bus->lock, K_FOREVER);
            if (bus->mode == IDU_PORT_POLL) { idu_collect_poll(poll); }
            k_mutex_unlock(&bus->lock);
            k_mutex_lock(&schedule_lock, K_FOREVER);
            next_poll[poll] = k_uptime_get() + equipment->collect[poll].period_ms;
            k_mutex_unlock(&schedule_lock);
        } else { k_sleep(K_MSEC(5)); }
    }
}

int idu_collect_start(void)
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
        k_thread_name_set(&bus->thread, "idu_bus");
    }
    return 0;
}
