#include <collect/modbus.h>
#include "client.h"
#include <modbus_support/transport.h>
#include <modbus_support/rtu.h>
#include <string.h>
#include <errno.h>

struct raw_port {
    bool allocated, registered;
    int iface;
    struct vport_binding binding;
    struct vport_source source;
    atomic_t receiving;
    struct k_mutex transaction_lock;
    struct k_spinlock state_lock;
    k_tid_t owner;
    struct vport_source destination;
};
static struct raw_port ports[CONFIG_MODBUS_NUMOF_RAW_ADU];
static K_MUTEX_DEFINE(bind_lock);

static void finished(void *user)
{
    struct raw_port *port = user;
    if (!atomic_get(&port->receiving)) { return; }
    co_port_complete(port->binding.port, &port->source);
    atomic_clear(&port->receiving);
}

static int receive(void *user, const struct vport_source *source,
                    const struct vport_frame *frame)
{
    struct raw_port *port = user;
    int err = mb_rtu_validate(frame->data, frame->size);
    if (err) { return err; }
    k_spinlock_key_t key = k_spin_lock(&port->state_lock);
    const struct vport_source *expected = port->owner ? &port->destination :
                                                       &port->binding.io->destination;
    if (source->link != expected->link || source->peer != expected->peer ||
        source->dev_id != expected->dev_id) {
        k_spin_unlock(&port->state_lock, key); return -EACCES;
    }
    if (!atomic_cas(&port->receiving, 0, 1)) {
        k_spin_unlock(&port->state_lock, key); return -EBUSY;
    }
    port->source = *source;
    k_spin_unlock(&port->state_lock, key);
    struct modbus_adu *adu = k_malloc(sizeof(*adu));
    if (!adu) { finished(port); return -ENOMEM; }
    err = mb_rtu_decode(frame->data, frame->size, adu);
    if (!err) { err = modbus_raw_submit_rx(port->iface, adu); }
    k_free(adu); /* Zephyr copies the complete ADU before returning. */
    if (err) { finished(port); }
    return err;
}

static int send(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    struct raw_port *port = user;
    k_spinlock_key_t key = k_spin_lock(&port->state_lock);
    if (port->owner && port->owner != k_current_get()) {
        k_spin_unlock(&port->state_lock, key); return -EBUSY;
    }
    struct vport_source destination = port->owner ? port->destination :
                                                   port->binding.io->destination;
    k_spin_unlock(&port->state_lock, key);
    if (adu->length > sizeof(adu->data)) { return -EMSGSIZE; }
    uint8_t *data = k_malloc(adu->length + 4U);
    if (!data) { return -ENOMEM; }
    size_t size;
    int err = mb_rtu_encode(adu, data, adu->length + 4U, &size);
    if (err) { k_free(data); return err; }
    const struct vport_frame frame = {.data = data, .size = size};
    k_timeout_t remaining = mb_client_remaining(port->iface);
    if (K_TIMEOUT_EQ(remaining, K_NO_WAIT)) { k_free(data); return -EBUSY; }
    k_timeout_t send_timeout = K_MSEC(CONFIG_CO_MODBUS_TX_TIMEOUT_MS);
    if (!K_TIMEOUT_EQ(remaining, K_FOREVER)) {
        send_timeout = K_TICKS(MIN(send_timeout.ticks, remaining.ticks));
    }
    err = co_port_send(port->binding.port, &destination, &frame, send_timeout);
    k_free(data); /* The send callback completes or copies the frame. */
    return err;
}

int co_modbus_port_bind(uint16_t id, const char *name, const struct vport_io *io)
{
    if (!name || !io || !io->send || io->mtu < CONFIG_MODBUS_BUFFER_SIZE || k_is_in_isr()) {
        return -EINVAL;
    }
    int iface = modbus_iface_get_by_name(name);
    if (iface < 0) { return iface; }
    k_mutex_lock(&bind_lock, K_FOREVER);
    struct raw_port *port = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].allocated && (ports[i].iface == iface || ports[i].binding.port == id)) {
            if (ports[i].iface != iface || ports[i].binding.port != id || ports[i].binding.io != io) {
                k_mutex_unlock(&bind_lock); return -EEXIST;
            }
            port = &ports[i]; break;
        }
        if (!ports[i].allocated && !port) { port = &ports[i]; }
    }
    int err = -ENOSPC;
    if (port && port->registered) { err = 0; }
    else if (port) {
        *port = (struct raw_port){.allocated = true, .iface = iface,
            .binding = {.port = id, .io = io, .receive = receive, .protocol = port}};
        k_mutex_init(&port->transaction_lock);
        err = mb_raw_set_done(iface, finished, port);
        if (!err) {
            err = co_port_register(&port->binding);
            if (err) { mb_raw_set_done(iface, NULL, port); }
        }
        port->registered = !err;
        if (err) { port->allocated = false; }
    }
    k_mutex_unlock(&bind_lock);
    return err;
}

int co_modbus_raw_transfer(int iface, uint16_t dev_id, struct modbus_adu *adu,
                          k_timeout_t timeout,
                          int (*transfer)(int, struct modbus_adu *, k_timeout_t))
{
    struct raw_port *port = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].registered && ports[i].iface == iface) { port = &ports[i]; break; }
    }
    if (!port) { return transfer(iface, adu, timeout); }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    if (k_mutex_lock(&port->transaction_lock, sys_timepoint_timeout(deadline))) { return -EBUSY; }
    k_spinlock_key_t key = k_spin_lock(&port->state_lock);
    if (port->owner || atomic_get(&port->receiving)) {
        k_spin_unlock(&port->state_lock, key);
        k_mutex_unlock(&port->transaction_lock); return -EBUSY;
    }
    port->destination = port->binding.io->destination;
    port->destination.dev_id = dev_id;
    port->owner = k_current_get();
    k_spin_unlock(&port->state_lock, key);
    int err = transfer(iface, adu, sys_timepoint_timeout(deadline));
    key = k_spin_lock(&port->state_lock);
    port->owner = NULL;
    k_spin_unlock(&port->state_lock, key);
    k_mutex_unlock(&port->transaction_lock);
    return err;
}

int co_modbus_port_param(int iface, struct modbus_iface_param *param)
{
    if (!param) { return -EINVAL; }
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].registered && ports[i].iface == iface) {
            if (param->mode != MODBUS_MODE_RAW) { return -EPROTOTYPE; }
            param->rawcb.raw_tx_cb = send;
            param->rawcb.user_data = &ports[i];
            return 0;
        }
    }
    return 0; /* Direct Zephyr RAW/RTU users may deliberately omit vport. */
}
