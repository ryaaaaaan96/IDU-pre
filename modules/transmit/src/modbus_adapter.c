#include <transmit/modbus_adapter.h>
#include <transmit/transmit.h>
#include <transmit_lib.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include "modbus_hooks.h"

/* Zephyr exposes at most four RAW interfaces. Leave room for serial ports. */
#define IFACE_LIMIT 16
static tr_modbus_write_fn writers[IFACE_LIMIT];
static struct tr_modbus_policy policies[IFACE_LIMIT];
struct port_callback {
    tr_modbus_route_fn handler;
    void *user;
    TR_FUNC_CB reference;
};
struct port_control {
    int64_t last_activity;
    bool connection_enabled, seen_activity;
    enum tr_port_mode mode;
    struct port_callback slave, callback, filter;
    bool filter_enabled;
    struct tr_port_stats stats;
};
static struct port_control controls[IFACE_LIMIT];
static TR_FUNC_CB reference_handlers[MB_MODE_MAX];
static struct k_spinlock control_lock;
struct forwarding_port {
    int iface;
    bool allocated;
    bool starting;
    struct k_work *startup_work;
    struct k_work_q queue;
    struct k_work_delayable reconfigure;
    struct modbus_iface_param current, pending;
    tr_modbus_config_done_fn done;
    void *user;
    int64_t deadline;
    int64_t busy_since;
    atomic_t paused, last_activity;
    uint32_t recoveries;
    int last_error;
    bool changing, plugged, force;
};
static struct forwarding_port ports[CONFIG_TR_PORTS];
K_THREAD_STACK_ARRAY_DEFINE(forward_stacks, CONFIG_TR_PORTS, CONFIG_TR_STACK_SIZE);
static struct { tr_modbus_route_fn fn; void *user; k_tid_t caller; } routes[IFACE_LIMIT];
K_MUTEX_DEFINE(config_lock);
static struct k_work_q config_queue;
K_THREAD_STACK_DEFINE(config_stack, CONFIG_TR_CONFIG_STACK_SIZE);
static bool config_started;
static struct k_spinlock submission_lock;

int tr_port_get_mode(int iface, enum tr_port_mode *mode)
{
    if (iface < 0 || iface >= IFACE_LIMIT || !mode) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    *mode = controls[iface].mode;
    k_spin_unlock(&control_lock, key);
    return 0;
}

int tr_port_set_mode(int iface, enum tr_port_mode mode)
{
    if (iface < 0 || iface >= IFACE_LIMIT || (unsigned)mode > TR_MODE_CALLBACK) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    int err = mode == TR_MODE_CALLBACK && !controls[iface].callback.handler &&
              !reference_handlers[MB_MODE_CALLBACK] ? -ENOENT : 0;
    if (!err) { controls[iface].mode = mode; }
    k_spin_unlock(&control_lock, key);
    return err;
}

int tr_port_set_handler(int iface, enum tr_port_mode mode,
                       tr_modbus_route_fn handler, void *user)
{
    if (iface < 0 || iface >= IFACE_LIMIT ||
        (mode != TR_MODE_SLAVE && mode != TR_MODE_CALLBACK)) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    struct port_control *port = &controls[iface];
    int err = mode == TR_MODE_CALLBACK && port->mode == mode && !handler &&
              !reference_handlers[MB_MODE_CALLBACK] ? -EBUSY : 0;
    if (!err) {
        struct port_callback *target = mode == TR_MODE_SLAVE ? &port->slave : &port->callback;
        *target = (struct port_callback){.handler = handler, .user = user};
    }
    k_spin_unlock(&control_lock, key);
    return err;
}

int tr_port_set_filter(int iface, tr_modbus_route_fn handler, void *user)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    controls[iface].filter = (struct port_callback){.handler = handler, .user = user};
    if (!handler) { controls[iface].filter_enabled = false; }
    k_spin_unlock(&control_lock, key);
    return 0;
}

int tr_port_enable_filter(int iface, bool enabled)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    int err = enabled && !controls[iface].filter.handler &&
              !controls[iface].filter.reference ? -ENOENT : 0;
    if (!err) { controls[iface].filter_enabled = enabled; }
    k_spin_unlock(&control_lock, key);
    return err;
}

int tr_port_control(int iface, enum tr_port_control command, void *arg)
{
    if (iface < 0 || iface >= IFACE_LIMIT ||
        (command != TR_PORT_GET_STATS && command != TR_PORT_CLEAR_STATS) ||
        (command == TR_PORT_GET_STATS && !arg) ||
        (command == TR_PORT_CLEAR_STATS && arg)) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    if (command == TR_PORT_GET_STATS) { *(struct tr_port_stats *)arg = controls[iface].stats; }
    else { controls[iface].stats = (struct tr_port_stats){0}; }
    k_spin_unlock(&control_lock, key);
    return 0;
}

int tr_reg_table_func(MB_MODE_E mode, TR_FUNC_CB handler)
{
    if (mode != MB_MODE_SLAVE && mode != MB_MODE_CALLBACK) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    if (mode == MB_MODE_CALLBACK && !handler) {
        for (size_t i = 0; i < ARRAY_SIZE(controls); ++i) {
            if (controls[i].mode == TR_MODE_CALLBACK && !controls[i].callback.handler) {
                k_spin_unlock(&control_lock, key); return -EBUSY;
            }
        }
    }
    reference_handlers[mode] = handler;
    k_spin_unlock(&control_lock, key);
    return 0;
}

int tr_reg_pkt_filter_func(uint16_t iface, TR_FUNC_CB filter)
{
    if (iface >= IFACE_LIMIT) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    controls[iface].filter = (struct port_callback){.reference = filter};
    if (!filter) { controls[iface].filter_enabled = false; }
    k_spin_unlock(&control_lock, key);
    return 0;
}

int tr_pkt_filter_config(uint16_t iface, bool enabled)
{
    return tr_port_enable_filter(iface, enabled);
}

int tr_get_port_mode(uint16_t iface, uint8_t *mode)
{
    if (!mode) { return -EINVAL; }
    enum tr_port_mode current;
    int err = tr_port_get_mode(iface, &current);
    if (!err) { *mode = (uint8_t)current; }
    return err;
}

int tr_set_port_mode(uint16_t iface, uint8_t mode)
{
    return tr_port_set_mode(iface, (enum tr_port_mode)mode);
}

int tr_mb_port_work_control(uint16_t iface, uint16_t command, void *arg)
{
    if (iface >= IFACE_LIMIT || command >= MB_PORT_CTRL_ENUM_MAX) { return -EINVAL; }
    if (command != MB_PORT_GET_RECV_MB_TIMES && command != MB_PORT_CLR_RECV_MB_TIMES) {
        return -ENOTSUP;
    }
    if ((command == MB_PORT_GET_RECV_MB_TIMES && !arg) ||
        (command == MB_PORT_CLR_RECV_MB_TIMES && arg)) { return -EINVAL; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    if (command == MB_PORT_GET_RECV_MB_TIMES) { *(uint32_t *)arg = controls[iface].stats.requests; }
    else { controls[iface].stats.requests = 0; }
    k_spin_unlock(&control_lock, key);
    return 0;
}

static struct forwarding_port *port_for(int iface)
{
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].allocated && ports[i].iface == iface) { return &ports[i]; }
    }
    return NULL;
}

static void reconfigure_work(struct k_work *work)
{
    struct forwarding_port *port = CONTAINER_OF(k_work_delayable_from_work(work),
                                                struct forwarding_port, reconfigure);
    struct modbus_context *ctx = modbus_get_context(port->iface);
    int err = 0;
    /* Stop new submissions before waiting. Never block this control queue in
     * drain/cancel_sync on a stuck business callback: enforce a deadline first. */
    k_spinlock_key_t key = k_spin_lock(&submission_lock);
    atomic_set(&port->paused, 1);
    k_spin_unlock(&submission_lock, key);
    if (ctx && k_work_busy_get(&ctx->server_work)) {
        if (k_uptime_get() < port->deadline) {
            k_work_reschedule_for_queue(&config_queue, &port->reconfigure, K_MSEC(5));
            return;
        }
        err = -ETIMEDOUT;
        goto complete;
    }
    if (!port->plugged) {
        k_work_queue_drain(&port->queue, true);
        port->plugged = true;
    }
    /* Serial RX resumes only after TX complete/DE release. Busy work cannot be
     * cancelled from itself, so configuration runs on a separate queue. */
    bool idle = ctx && !k_work_busy_get(&ctx->server_work) &&
        (port->force || ctx->mode == MODBUS_MODE_RAW || atomic_test_bit(&ctx->state, MODBUS_STATE_RX_ENABLED));
    if (!idle && k_uptime_get() < port->deadline) {
        k_work_reschedule_for_queue(&config_queue, &port->reconfigure, K_MSEC(5)); return;
    }
    if (!idle) { err = -ETIMEDOUT; }
    else {
        err = modbus_disable(port->iface);
        if (!err) { err = modbus_init_server(port->iface, port->pending); }
        if (err) {
            if (modbus_init_server(port->iface, port->current)) { err = -ENOTRECOVERABLE; }
        } else { port->current = port->pending; }
    }
complete:
    if (port->plugged) { k_work_queue_unplug(&port->queue); port->plugged = false; }
    k_mutex_lock(&config_lock, K_FOREVER);
    tr_modbus_config_done_fn done = port->done; void *user = port->user;
    port->changing = false;
    port->last_error = err;
    if (port->force) { ++port->recoveries; }
    atomic_set(&port->last_activity, k_uptime_get_32());
    atomic_clear(&port->paused);
    k_mutex_unlock(&config_lock);
    if (done) { done(port->iface, err, user); }
}

int tr_modbus_init_server(int iface, struct modbus_iface_param param,
                           tr_modbus_write_fn write_fn, bool isolated)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    k_mutex_lock(&config_lock, K_FOREVER);
    struct forwarding_port *port = port_for(iface);
    if (port && port->changing) { k_mutex_unlock(&config_lock); return -EBUSY; }
    int err = tr_modbus_check_available(iface);
    if (err) { k_mutex_unlock(&config_lock); return err; }

    bool reserved = false;
    if (isolated && !port) {
        for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
            if (ports[i].allocated) { continue; }
            port = &ports[i];
            k_spinlock_key_t key = k_spin_lock(&submission_lock);
            port->iface = iface;
            port->starting = true;
            atomic_set(&port->paused, 1);
            port->allocated = true;
            k_spin_unlock(&submission_lock, key);
            reserved = true;
            break;
        }
        if (!port) { k_mutex_unlock(&config_lock); return -ENOSPC; }
    }
    if (port) {
        k_spinlock_key_t key = k_spin_lock(&submission_lock);
        port->starting = true;
        port->startup_work = NULL;
        atomic_set(&port->paused, 1);
        k_spin_unlock(&submission_lock, key);
    }
    tr_modbus_write_fn previous_writer = writers[iface];
    writers[iface] = write_fn;
    err = modbus_init_server(iface, param);
    if (!err && port) {
        /* RX stays paused until the worker exists. Failed startup consumes no slot. */
        if (reserved) {
            size_t index = port - ports;
            k_work_queue_start(&port->queue, forward_stacks[index],
                               K_THREAD_STACK_SIZEOF(forward_stacks[index]), 6, NULL);
            k_work_init_delayable(&port->reconfigure, reconfigure_work);
        }
        port->current = param; port->last_error = 0;
        port->busy_since = 0;
        atomic_set(&port->last_activity, k_uptime_get_32());
    }
    if (err) { writers[iface] = previous_writer; }
    if (port) {
        k_spinlock_key_t key = k_spin_lock(&submission_lock);
        if (err && reserved) { port->allocated = false; }
        port->starting = false;
        atomic_clear(&port->paused);
        /* UART RX may complete before init returns. Preserve that notification
         * so a full RX buffer can be consumed and reception enabled again.
         */
        if (!err && port->startup_work) {
            k_work_submit_to_queue(&port->queue, port->startup_work);
        }
        port->startup_work = NULL;
        k_spin_unlock(&submission_lock, key);
    }
    k_mutex_unlock(&config_lock);
    return err;
}

int tr_modbus_set_router(int iface, tr_modbus_route_fn route, void *user)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    /* Configure at startup, before the interface receives requests. */
    routes[iface].fn = route; routes[iface].user = user; return 0;
}

int tr_modbus_set_policy(int iface, const struct tr_modbus_policy *policy)
{
    if (iface < 0 || iface >= IFACE_LIMIT || (policy && !policy->accept)) { return -EINVAL; }
    policies[iface] = policy ? *policy : (struct tr_modbus_policy){0};
    return 0;
}

bool tr_modbus_accept_request(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    if (iface < 0 || iface >= IFACE_LIMIT) { return false; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    bool sleeping = controls[iface].mode == TR_MODE_SLEEP;
    if (sleeping) { ++controls[iface].stats.sleep_drops; }
    k_spin_unlock(&control_lock, key);
    if (sleeping) { return false; }
    const struct tr_modbus_policy *p = &policies[iface];
    bool accepted = p->accept ? p->accept(&ctx->rx_adu, ctx->unit_id, p->user) :
                    (ctx->rx_adu.unit_id == 0 || ctx->rx_adu.unit_id == ctx->unit_id);
    struct forwarding_port *port = port_for(iface);
    if (accepted && port) { atomic_set(&port->last_activity, k_uptime_get_32()); }
    return accepted;
}

bool tr_modbus_suppress_broadcast(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    return ctx->rx_adu.unit_id == 0 && (iface < 0 || iface >= IFACE_LIMIT ||
                                       !policies[iface].legacy_broadcast);
}

int tr_modbus_request_iface(void)
{
    for (int i = 0; i < IFACE_LIMIT; ++i) {
        if (routes[i].caller == k_current_get()) { return i; }
    }
    return -1;
}

uint8_t tr_modbus_request_function(void)
{
    int iface = tr_modbus_request_iface();
    struct modbus_context *ctx = iface < 0 ? NULL : modbus_get_context(iface);
    return ctx ? ctx->rx_adu.fc : 0;
}

static int schedule_config(int iface, const struct modbus_iface_param *param,
                            tr_modbus_config_done_fn done, void *user)
{
    k_mutex_lock(&config_lock, K_FOREVER);
    struct forwarding_port *port = port_for(iface);
    if (!port || port->changing) { k_mutex_unlock(&config_lock); return port ? -EBUSY : -ENODEV; }
    if (!config_started) {
        k_work_queue_start(&config_queue, config_stack, K_THREAD_STACK_SIZEOF(config_stack), 8, NULL);
        config_started = true;
    }
    port->pending = param ? *param : port->current;
    port->force = param == NULL; port->done = done; port->user = user;
    port->changing = true; port->deadline = k_uptime_get() + 2000;
    k_work_reschedule_for_queue(&config_queue, &port->reconfigure, K_MSEC(20));
    k_mutex_unlock(&config_lock); return 0;
}

int tr_modbus_reconfigure(int iface, struct modbus_iface_param param,
                           tr_modbus_config_done_fn done, void *user)
{
    if ((unsigned)param.mode > MODBUS_MODE_RAW || !param.server.user_cb ||
        !param.server.unit_id || param.server.unit_id > 247 ||
        (param.mode == MODBUS_MODE_RAW && !param.rawcb.raw_tx_cb) ||
        (param.mode != MODBUS_MODE_RAW && (!param.serial.baud ||
         (unsigned)param.serial.parity > UART_CFG_PARITY_EVEN ||
         (param.serial.stop_bits != UART_CFG_STOP_BITS_1 && param.serial.stop_bits != UART_CFG_STOP_BITS_2)))) {
        return -EINVAL;
    }
    return schedule_config(iface, &param, done, user);
}

int tr_modbus_recover(int iface, tr_modbus_config_done_fn done, void *user)
{
    return schedule_config(iface, NULL, done, user);
}

int tr_modbus_get_health(int iface, struct tr_modbus_health *health)
{
    if (!health) { return -EINVAL; }
    k_mutex_lock(&config_lock, K_FOREVER);
    struct forwarding_port *port = port_for(iface);
    if (port) { *health = (struct tr_modbus_health){port->recoveries, port->last_error, port->changing}; }
    k_mutex_unlock(&config_lock);
    return port ? 0 : -ENODEV;
}

int tr_modbus_service(int iface, uint32_t timeout_ms)
{
    if (!timeout_ms || timeout_ms > INT32_MAX) { return -EINVAL; }
    if (k_mutex_lock(&config_lock, K_NO_WAIT)) { return 0; }
    struct forwarding_port *port = port_for(iface);
    if (!port) { k_mutex_unlock(&config_lock); return -ENODEV; }
    if (port->last_error == -ENOTRECOVERABLE) {
        k_mutex_unlock(&config_lock); return -ENOTRECOVERABLE;
    }
    struct modbus_context *ctx = modbus_get_context(iface);
    int64_t now = k_uptime_get();
    if (!ctx) {
        int err = port->changing ? (now > port->deadline + timeout_ms ? -ETIMEDOUT : 0) : -ENODEV;
        k_mutex_unlock(&config_lock); return err;
    }
    bool busy = k_work_busy_get(&ctx->server_work) != 0;
    if (busy && !port->busy_since) { port->busy_since = now; }
    if (!busy) { port->busy_since = 0; }
    int err = 0;
    if ((busy && now - port->busy_since >= timeout_ms) ||
        (port->changing && now > port->deadline + timeout_ms)) { err = -ETIMEDOUT; }
    bool recover = !err && !busy && !port->changing &&
        (uint32_t)((uint32_t)now - (uint32_t)atomic_get(&port->last_activity)) >= timeout_ms;
    k_mutex_unlock(&config_lock);
    if (recover) {
        err = tr_modbus_recover(iface, NULL, NULL);
        if (err == -EBUSY) { err = 0; }
    }
    return err;
}

int tr_modbus_submit_work(struct k_work *work)
{
    struct modbus_context *ctx = CONTAINER_OF(work, struct modbus_context, server_work);
    k_spinlock_key_t key = k_spin_lock(&submission_lock);
    struct forwarding_port *port = port_for(modbus_iface_get_by_ctx(ctx));
    int err;
    if (port && port->starting) {
        port->startup_work = work;
        err = 0;
    } else {
        err = port && atomic_get(&port->paused) ? -EBUSY :
              port ? k_work_submit_to_queue(&port->queue, work) : k_work_submit(work);
    }
    k_spin_unlock(&submission_lock, key);
    return err;
}

static uint8_t exception_from_error(int err)
{
    switch (err) {
    case -ENOENT: case -ENOTSUP: case -EACCES: return MODBUS_EXC_ILLEGAL_DATA_ADDR;
    case -EINVAL: case -ERANGE: case -EXDEV: return MODBUS_EXC_ILLEGAL_DATA_VAL;
    case -EBUSY: case -EAGAIN: return MODBUS_EXC_SERVER_DEVICE_BUSY;
    case -ETIMEDOUT: return MODBUS_EXC_GW_TARGET_FAILED_TO_RESP;
    default: return MODBUS_EXC_SERVER_DEVICE_FAILURE;
    }
}

/* No control locks are held across application callbacks. One snapshot governs
 * each admitted request; changing mode never cancels a handler or serial TX.
 */
static int call_handler(int iface, struct modbus_context *ctx,
                         struct port_callback callback, bool filter)
{
    routes[iface].caller = k_current_get();
    int err;
    if (callback.reference) {
        int result = callback.reference((uint16_t)iface, &ctx->rx_adu, &ctx->tx_adu);
        if (filter) {
            err = result == 0 ? -ENOENT : result == 4 ? TR_MODBUS_SILENT : -ENOTSUP;
        } else {
            err = result == -1 ? -ENOENT : result == 0 ? 0 : -ENOTSUP;
        }
    } else {
        err = callback.handler(iface, &ctx->rx_adu, &ctx->tx_adu, callback.user);
    }
    routes[iface].caller = NULL;
    if (!err && ctx->tx_adu.length > sizeof(ctx->tx_adu.data)) { err = -EMSGSIZE; }
    if (err && err != -ENOENT && err != TR_MODBUS_SILENT) {
        k_spinlock_key_t key = k_spin_lock(&control_lock);
        ++controls[iface].stats.callback_errors;
        k_spin_unlock(&control_lock, key);
    }
    return err;
}

static bool callback_reply(struct modbus_context *ctx, int routed, bool *reply)
{
    if (routed == TR_MODBUS_SILENT) { *reply = false; return true; }
    *reply = true;
    if (routed) {
        ctx->tx_adu.fc = ctx->rx_adu.fc | 0x80;
        ctx->tx_adu.length = 1;
        ctx->tx_adu.data[0] = routed > 0 && routed < 256 ? routed : exception_from_error(routed);
    }
    return true;
}

void tr_modbus_complete_request(struct modbus_context *ctx, bool reply)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    if (iface < 0 || iface >= IFACE_LIMIT || !reply) { return; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    ++controls[iface].stats.responses;
    k_spin_unlock(&control_lock, key);
}

bool tr_modbus_write_request(struct modbus_context *ctx, bool *reply)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    const struct modbus_adu *rx = &ctx->rx_adu;
    if (iface < 0 || iface >= IFACE_LIMIT) { return false; }
    k_spinlock_key_t key = k_spin_lock(&control_lock);
    struct port_control control = controls[iface];
    if (!control.slave.handler) { control.slave.reference = reference_handlers[MB_MODE_SLAVE]; }
    if (!control.callback.handler) { control.callback.reference = reference_handlers[MB_MODE_CALLBACK]; }
    ++controls[iface].stats.requests;
    if (controls[iface].connection_enabled && control.mode != TR_MODE_SLEEP) {
        controls[iface].last_activity = k_uptime_get();
        controls[iface].seen_activity = true;
    }
    if (control.mode == TR_MODE_SLEEP) { ++controls[iface].stats.sleep_drops; }
    k_spin_unlock(&control_lock, key);
    if (control.mode == TR_MODE_SLEEP) { return callback_reply(ctx, TR_MODBUS_SILENT, reply); }
    if (rx->unit_id != 0 || policies[iface].legacy_broadcast) {
        if (control.filter_enabled && (control.filter.handler || control.filter.reference)) {
            int routed = call_handler(iface, ctx, control.filter, true);
            if (routed != -ENOENT) {
                key = k_spin_lock(&control_lock);
                ++controls[iface].stats.filtered;
                k_spin_unlock(&control_lock, key);
                return callback_reply(ctx, routed, reply);
            }
        }
        struct port_callback handler = control.mode == TR_MODE_CALLBACK ?
                                       control.callback : control.slave;
        int routed = handler.handler || handler.reference ? call_handler(iface, ctx, handler, false) : -ENOENT;
        if (control.mode == TR_MODE_CALLBACK || routed != -ENOENT) {
            return callback_reply(ctx, routed == -ENOENT ? -ENOTSUP : routed, reply);
        }
        if (routes[iface].fn) {
            handler = (struct port_callback){.handler = routes[iface].fn, .user = routes[iface].user};
            routed = call_handler(iface, ctx, handler, false);
            if (routed != -ENOENT) { return callback_reply(ctx, routed, reply); }
        }
    }
    if (control.mode == TR_MODE_CALLBACK) { return callback_reply(ctx, TR_MODBUS_SILENT, reply); }
    if (!writers[iface] || (rx->fc != 6 && rx->fc != 16)) {
        return false;
    }
    *reply = rx->unit_id != 0;
    if (!*reply) {
        return true; /* Parameter broadcasts have no side effects. */
    }
    int err = -EINVAL;
    uint16_t words[123];
    uint16_t address = 0;
    uint16_t count = 0;
    if (rx->fc == 6 && rx->length == 4) {
        address = sys_get_be16(rx->data);
        count = 1;
        words[0] = sys_get_be16(rx->data + 2);
        err = 0;
    } else if (rx->fc == 16 && rx->length >= 7) {
        address = sys_get_be16(rx->data);
        count = sys_get_be16(rx->data + 2);
        if (count && count <= ARRAY_SIZE(words) && rx->data[4] == count * 2 &&
            rx->length == 5 + count * 2 && (uint32_t)address + count <= 65536) {
            for (unsigned i = 0; i < count; ++i) {
                words[i] = sys_get_be16(rx->data + 5 + i * 2);
            }
            err = 0;
        }
    }
    if (!err) {
        routes[iface].caller = k_current_get();
        err = writers[iface](address, words, count);
        routes[iface].caller = NULL;
    }
    if (err) {
        ctx->tx_adu.fc |= 0x80;
        ctx->tx_adu.length = 1;
        ctx->tx_adu.data[0] = exception_from_error(err);
    } else {
        ctx->tx_adu.length = 4;
        sys_put_be16(address, ctx->tx_adu.data);
        sys_put_be16(rx->fc == 6 ? words[0] : count, ctx->tx_adu.data + 2);
    }
    return true;
}

int tr_port_conn_status_config(uint16_t iface, bool enabled)
{
    if (iface >= IFACE_LIMIT || k_is_in_isr()) { return -EINVAL; }
    k_spinlock_key_t lock = k_spin_lock(&control_lock);
    controls[iface].connection_enabled = enabled;
    controls[iface].seen_activity = false;
    controls[iface].last_activity = 0;
    k_spin_unlock(&control_lock, lock);
    return 0;
}

int tr_port_connection_get(int iface, struct tr_connection_status *status)
{
    if (iface < 0 || iface >= IFACE_LIMIT || !status) { return -EINVAL; }
    k_spinlock_key_t lock = k_spin_lock(&control_lock);
    const struct port_control *control = &controls[iface];
    *status = (struct tr_connection_status){.enabled = control->connection_enabled,
        .last_activity = control->last_activity,
        .connected = control->connection_enabled && control->seen_activity &&
                     control->mode != TR_MODE_SLEEP &&
                     k_uptime_get() - control->last_activity < CONFIG_TR_CONN_TIMEOUT_MS};
    k_spin_unlock(&control_lock, lock);
    return 0;
}
