#include <vport/vport.h>
#include <errno.h>

int vport_bind(vport_t *port, vport_receive_t receive, void *context)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !port->send || !receive) {
        return -EINVAL;
    }
    if (port->receive) {
        return -EALREADY;
    }
    port->receive = receive;
    port->receive_context = context;
    return 0;
}

int vport_input(vport_t *port, const vport_event_t *event)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !event ||
        (event->type == VPORT_DATA ? !event->data || !event->size : event->size != 0)) {
        return -EINVAL;
    }
    switch (event->type) {
    case VPORT_DATA:
    case VPORT_IDLE:
    case VPORT_POLL:
    case VPORT_ERROR:
        break;
    default:
        return -EINVAL;
    }
    if (!port->receive) {
        return -ENODEV;
    }
    return port->receive(port->receive_context, event);
}

int vport_output(vport_t *port, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !port->send || !data || !size) {
        return -EINVAL;
    }
    int err = port->send(port->send_context, data, size, timeout);
    return err > 0 ? -EIO : err;
}
