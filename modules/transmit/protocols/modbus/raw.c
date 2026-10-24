#include <transmit/modbus.h>
#include <modbus_support/transport.h>
#include <modbus_support/rtu.h>
#include <string.h>
#include <errno.h>

struct raw_port {
    bool allocated, registered;
    int iface;
    const char *name;
    struct vport_binding binding;
    struct vport_source source;
    atomic_t receiving;
};
static struct raw_port ports[CONFIG_MODBUS_NUMOF_RAW_ADU];
static K_MUTEX_DEFINE(bind_lock);

static void finished(void *user)
{
    struct raw_port *port = user;
    if (!atomic_get(&port->receiving)) { return; }
    tr_port_complete(port->binding.port, &port->source);
    atomic_clear(&port->receiving);
}

static int receive(void *user, const struct vport_source *source,
                    const struct vport_frame *frame)
{
    struct raw_port *port = user;
    int err = mb_rtu_validate(frame->data, frame->size);
    if (err) { return err; }

    if (!atomic_cas(&port->receiving, 0, 1)) { return -EBUSY; }
    port->source = *source;
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
    if (adu->length > sizeof(adu->data)) { return -EMSGSIZE; }
    uint8_t *data = k_malloc(adu->length + 4U);
    if (!data) { return -ENOMEM; }
    size_t size;
    int err = mb_rtu_encode(adu, data, adu->length + 4U, &size);
    if (err) { k_free(data); return err; }
    const struct vport_frame frame = {.data = data, .size = size};
    err = tr_port_send(port->binding.port, &port->source, &frame,
                             K_MSEC(CONFIG_TR_MODBUS_TX_TIMEOUT_MS));
    k_free(data); /* The send callback completes or copies the frame. */
    return err;
}

int tr_modbus_port_bind(uint16_t id, const char *name, const struct vport_io *io)
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
        *port = (struct raw_port){.allocated = true, .iface = iface, .name = name,
            .binding = {.port = id, .io = io, .receive = receive, .protocol = port}};
        err = mb_raw_set_done(iface, finished, port);
        if (!err) {
            err = tr_port_register(&port->binding);
            if (err) { mb_raw_set_done(iface, NULL, port); }
        }
        port->registered = !err;
        if (err) { port->allocated = false; }
    }
    k_mutex_unlock(&bind_lock);
    return err;
}

int tr_modbus_port_param(int iface, struct modbus_iface_param *param)
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

const char *tr_modbus_port_name(uint16_t id)
{
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].registered && ports[i].binding.port == id) { return ports[i].name; }
    }
    return NULL;
}
