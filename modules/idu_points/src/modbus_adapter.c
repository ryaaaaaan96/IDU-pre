#include <idu/modbus_adapter.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include "modbus_internal.h"

/* Zephyr exposes at most four RAW interfaces. Leave room for serial ports. */
#define IFACE_LIMIT 16
static idu_modbus_write_fn writers[IFACE_LIMIT];
static struct idu_modbus_policy policies[IFACE_LIMIT];
struct forwarding_port {
    int iface;
    bool allocated;
    struct k_work_q queue;
    struct k_work_delayable reconfigure;
    struct modbus_iface_param current, pending;
    idu_modbus_config_done_fn done;
    void *user;
    int64_t deadline;
    int64_t busy_since;
    atomic_t paused, last_activity;
    uint32_t recoveries;
    int last_error;
    bool changing, plugged, force;
};
static struct forwarding_port ports[CONFIG_IDU_FORWARD_PORTS];
K_THREAD_STACK_ARRAY_DEFINE(forward_stacks, CONFIG_IDU_FORWARD_PORTS, CONFIG_IDU_FORWARD_STACK_SIZE);
static struct { idu_modbus_route_fn fn; void *user; k_tid_t caller; } routes[IFACE_LIMIT];
K_MUTEX_DEFINE(config_lock);
static struct k_work_q config_queue;
K_THREAD_STACK_DEFINE(config_stack, 2048);
static bool config_started;
static struct k_spinlock submission_lock;

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
    idu_modbus_config_done_fn done = port->done; void *user = port->user;
    port->changing = false;
    port->last_error = err;
    if (port->force) { ++port->recoveries; }
    atomic_set(&port->last_activity, k_uptime_get_32());
    atomic_clear(&port->paused);
    k_mutex_unlock(&config_lock);
    if (done) { done(port->iface, err, user); }
}

int idu_modbus_init_server(int iface, struct modbus_iface_param param,
                           idu_modbus_write_fn write_fn, bool isolated)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    k_mutex_lock(&config_lock, K_FOREVER);
    struct forwarding_port *port = port_for(iface);
    if (isolated && !port) {
        for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
            if (ports[i].allocated) { continue; }
            port = &ports[i]; port->iface = iface; port->allocated = true;
            k_work_queue_start(&port->queue, forward_stacks[i],
                               K_THREAD_STACK_SIZEOF(forward_stacks[i]), 6, NULL);
            k_work_init_delayable(&port->reconfigure, reconfigure_work);
            break;
        }
        if (!port) { k_mutex_unlock(&config_lock); return -ENOSPC; }
    }
    if (port && port->changing) { k_mutex_unlock(&config_lock); return -EBUSY; }
    writers[iface] = write_fn;
    int err = modbus_init_server(iface, param);
    if (!err && port) {
        port->current = param; port->last_error = 0;
        atomic_set(&port->last_activity, k_uptime_get_32());
    }
    if (err) { writers[iface] = NULL; }
    k_mutex_unlock(&config_lock);
    return err;
}

int idu_modbus_set_router(int iface, idu_modbus_route_fn route, void *user)
{
    if (iface < 0 || iface >= IFACE_LIMIT) { return -EINVAL; }
    /* Configure at startup, before the interface receives requests. */
    routes[iface].fn = route; routes[iface].user = user; return 0;
}

int idu_modbus_set_policy(int iface, const struct idu_modbus_policy *policy)
{
    if (iface < 0 || iface >= IFACE_LIMIT || (policy && !policy->accept)) { return -EINVAL; }
    policies[iface] = policy ? *policy : (struct idu_modbus_policy){0};
    return 0;
}

bool idu_modbus_accept_request(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    if (iface < 0 || iface >= IFACE_LIMIT) { return false; }
    const struct idu_modbus_policy *p = &policies[iface];
    bool accepted = p->accept ? p->accept(&ctx->rx_adu, ctx->unit_id, p->user) :
                    (ctx->rx_adu.unit_id == 0 || ctx->rx_adu.unit_id == ctx->unit_id);
    struct forwarding_port *port = port_for(iface);
    if (accepted && port) { atomic_set(&port->last_activity, k_uptime_get_32()); }
    return accepted;
}

bool idu_modbus_suppress_broadcast(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    return ctx->rx_adu.unit_id == 0 && (iface < 0 || iface >= IFACE_LIMIT ||
                                       !policies[iface].legacy_broadcast);
}

int idu_modbus_request_iface(void)
{
    for (int i = 0; i < IFACE_LIMIT; ++i) {
        if (routes[i].caller == k_current_get()) { return i; }
    }
    return -1;
}

uint8_t idu_modbus_request_function(void)
{
    int iface = idu_modbus_request_iface();
    struct modbus_context *ctx = iface < 0 ? NULL : modbus_get_context(iface);
    return ctx ? ctx->rx_adu.fc : 0;
}

static int schedule_config(int iface, const struct modbus_iface_param *param,
                            idu_modbus_config_done_fn done, void *user)
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

int idu_modbus_reconfigure(int iface, struct modbus_iface_param param,
                           idu_modbus_config_done_fn done, void *user)
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

int idu_modbus_recover(int iface, idu_modbus_config_done_fn done, void *user)
{
    return schedule_config(iface, NULL, done, user);
}

int idu_modbus_get_health(int iface, struct idu_modbus_health *health)
{
    if (!health) { return -EINVAL; }
    k_mutex_lock(&config_lock, K_FOREVER);
    struct forwarding_port *port = port_for(iface);
    if (port) { *health = (struct idu_modbus_health){port->recoveries, port->last_error, port->changing}; }
    k_mutex_unlock(&config_lock);
    return port ? 0 : -ENODEV;
}

int idu_modbus_service(int iface, uint32_t timeout_ms)
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
        err = idu_modbus_recover(iface, NULL, NULL);
        if (err == -EBUSY) { err = 0; }
    }
    return err;
}

int idu_modbus_submit_work(struct k_work *work)
{
    struct modbus_context *ctx = CONTAINER_OF(work, struct modbus_context, server_work);
    struct forwarding_port *port = port_for(modbus_iface_get_by_ctx(ctx));
    k_spinlock_key_t key = k_spin_lock(&submission_lock);
    int err = port && atomic_get(&port->paused) ? -EBUSY :
              port ? k_work_submit_to_queue(&port->queue, work) : k_work_submit(work);
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

bool idu_modbus_write_request(struct modbus_context *ctx, bool *reply)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    const struct modbus_adu *rx = &ctx->rx_adu;
    if (iface < 0 || iface >= IFACE_LIMIT) { return false; }
    if (routes[iface].fn && (rx->unit_id != 0 || policies[iface].legacy_broadcast)) {
        routes[iface].caller = k_current_get();
        int routed = routes[iface].fn(iface, rx, &ctx->tx_adu, routes[iface].user);
        routes[iface].caller = NULL;
        if (routed != -ENOENT) {
            if (routed == IDU_MODBUS_SILENT) { *reply = false; return true; }
            *reply = true;
            if (routed) {
                ctx->tx_adu.fc = rx->fc | 0x80; ctx->tx_adu.length = 1;
                ctx->tx_adu.data[0] = routed > 0 && routed < 256 ? routed : exception_from_error(routed);
            }
            return true;
        }
    }
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
