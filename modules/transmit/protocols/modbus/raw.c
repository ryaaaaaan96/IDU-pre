#include <transmit/modbus.h>
#include <modbus_io.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <string.h>
#include <errno.h>

struct raw_port {
    bool allocated, registered;
    int iface;
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
    if (frame->size < 4 || frame->size > CONFIG_MODBUS_BUFFER_SIZE) { return -EMSGSIZE; }
    if (crc16_ansi(frame->data, frame->size - 2) != sys_get_le16(frame->data + frame->size - 2)) {
        return -EBADMSG;
    }

    if (!atomic_cas(&port->receiving, 0, 1)) { return -EBUSY; }
    port->source = *source;
    struct modbus_adu adu = {.unit_id = frame->data[0], .fc = frame->data[1],
        .length = frame->size - 4, .crc = sys_get_le16(frame->data + frame->size - 2)};
    memcpy(adu.data, frame->data + 2, adu.length);
    int err = modbus_raw_submit_rx(port->iface, &adu);
    if (err) { finished(port); }
    return err;
}

static int send(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    struct raw_port *port = user;
    if (adu->length > sizeof(adu->data)) { return -EMSGSIZE; }
    uint8_t data[CONFIG_MODBUS_BUFFER_SIZE];
    data[0] = adu->unit_id; data[1] = adu->fc;
    memcpy(data + 2, adu->data, adu->length);
    sys_put_le16(crc16_ansi(data, adu->length + 2), data + adu->length + 2);
    const struct vport_frame frame = {.data = data, .size = adu->length + 4};
    return tr_port_send(port->binding.port, &port->source, &frame,
                             K_MSEC(CONFIG_TR_MODBUS_TX_TIMEOUT_MS));
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
        *port = (struct raw_port){.allocated = true, .iface = iface,
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
