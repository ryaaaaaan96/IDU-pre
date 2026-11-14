#include <vport/vport.h>
#include <errno.h>

static vport_channel_t *find_channel(vport_t *port, vport_channel_id_t channel_id)
{
    for (vport_channel_t *channel = port->channels; channel; channel = channel->next) {
        if (channel->channel_id == channel_id) {
            return channel;
        }
    }
    return NULL;
}

int vport_channel_add(vport_t *port, vport_channel_t *channel)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !channel || !channel->channel_id || !channel->send) {
        return -EINVAL;
    }
    if (port->state != VPORT_INIT) {
        return -EBUSY;
    }
    if (channel->owner) {
        return -EALREADY;
    }
    if (find_channel(port, channel->channel_id)) {
        return -EEXIST;
    }
    channel->next = port->channels;
    channel->owner = port;
    port->channels = channel;
    return 0;
}

int vport_channel_bind(
    vport_t *port, vport_channel_id_t channel_id, vport_receive_t receive, void *context)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !channel_id || !receive) {
        return -EINVAL;
    }
    if (port->state != VPORT_INIT) {
        return -EBUSY;
    }
    vport_channel_t *channel = find_channel(port, channel_id);
    if (!channel) {
        return -ENODEV;
    }
    if (channel->receive) {
        return -EALREADY;
    }
    channel->receive = receive;
    channel->receive_context = context;
    return 0;
}

int vport_channel_unbind(vport_t *port, vport_channel_id_t channel_id)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !channel_id) {
        return -EINVAL;
    }
    if (port->state != VPORT_INIT && port->state != VPORT_STOPPED) {
        return -EBUSY;
    }
    vport_channel_t *channel = find_channel(port, channel_id);
    if (!channel) {
        return -ENODEV;
    }
    if (atomic_get(&channel->input_busy)) {
        return -EBUSY;
    }
    channel->receive = NULL;
    channel->receive_context = NULL;
    return 0;
}

int vport_reset(vport_t *port)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port) {
        return -EINVAL;
    }
    if (port->state != VPORT_INIT && port->state != VPORT_STOPPED) {
        return -EBUSY;
    }
    /* 先检查全部节点，失败时保留完整注册关系。 */
    for (vport_channel_t *channel = port->channels; channel; channel = channel->next) {
        if (channel->receive || atomic_get(&channel->input_busy) ||
            atomic_get(&channel->output_active)) {
            return -EBUSY;
        }
    }
    while (port->channels) {
        vport_channel_t *channel = port->channels;
        port->channels = channel->next;
        channel->source = NULL;
        channel->source_context = NULL;
        channel->receive_context = NULL;
        channel->next = NULL;
        channel->owner = NULL;
    }
    port->state = VPORT_INIT;
    return 0;
}

int vport_start(vport_t *port)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !port->channels) {
        return -EINVAL;
    }
    if (port->state != VPORT_INIT) {
        return -EBUSY;
    }
    for (vport_channel_t *channel = port->channels; channel; channel = channel->next) {
        if (!channel->send || !channel->receive) {
            return -ENOTCONN;
        }
    }
    port->state = VPORT_READY;
    return 0;
}

int vport_stop(vport_t *port)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port) {
        return -EINVAL;
    }
    if (port->state != VPORT_READY) {
        return -EBUSY;
    }
    port->state = VPORT_STOPPED;
    return 0;
}

static int dispatch_event(vport_t *port, const vport_event_t *event)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !event || !event->channel_id ||
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
    if (port->state != VPORT_READY) {
        return -EACCES;
    }
    vport_channel_t *channel = find_channel(port, event->channel_id);
    if (!channel) {
        return -ENODEV;
    }
    if (!channel->receive) {
        return -ENOTCONN;
    }
    return channel->receive(channel->receive_context, event);
}

int vport_input(vport_t *port, const vport_event_t *event)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !event || !event->channel_id) {
        return -EINVAL;
    }
    if (port->state != VPORT_READY) {
        return -EACCES;
    }
    vport_channel_t *channel = find_channel(port, event->channel_id);
    if (!channel) {
        return -ENODEV;
    }
    if (!atomic_cas(&channel->input_busy, 0, 1)) {
        return -EBUSY;
    }
    int err = dispatch_event(port, event);
    atomic_clear(&channel->input_busy);
    return err;
}

int vport_poll(vport_t *port, vport_channel_id_t channel_id)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !channel_id) {
        return -EINVAL;
    }
    if (port->state != VPORT_READY) {
        return -EACCES;
    }
    vport_channel_t *channel = find_channel(port, channel_id);
    if (!channel) {
        return -ENODEV;
    }
    if (!channel->source) {
        return -ENOTCONN;
    }
    if (!atomic_cas(&channel->input_busy, 0, 1)) {
        return -EBUSY;
    }
    vport_event_t event = {0};
    int err = channel->source(channel->source_context, &event);
    if (!err) {
        err = event.channel_id == channel_id ? dispatch_event(port, &event) : -EINVAL;
    } else if (err > 0) {
        err = -EPROTO;
    }
    atomic_clear(&channel->input_busy);
    return err;
}

int vport_output(vport_t *port,
    vport_channel_id_t channel_id,
    const uint8_t *data,
    size_t size,
    k_timeout_t timeout)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!port || !channel_id || !data || !size) {
        return -EINVAL;
    }
    if (port->state != VPORT_READY) {
        return -EACCES;
    }
    vport_channel_t *channel = find_channel(port, channel_id);
    if (!channel) {
        return -ENODEV;
    }
    atomic_inc(&channel->output_active);
    int err = channel->send(channel->send_context, data, size, timeout);
    atomic_dec(&channel->output_active);
    return err > 0 ? -EIO : err;
}
