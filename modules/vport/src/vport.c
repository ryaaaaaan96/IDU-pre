#include <vport/vport.h>
#include <errno.h>

struct port_state {
    const void *owner;
    const struct vport_binding *binding;
    struct k_mutex tx_lock;
};
static struct port_state ports[CONFIG_VPORT_MAX_PORTS];
static struct k_spinlock registry_lock;
K_MUTEX_DEFINE(register_lock);

static struct port_state *find(const void *owner, uint16_t id)
{
    struct port_state *result = NULL;
    k_spinlock_key_t key = k_spin_lock(&registry_lock);
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].binding && ports[i].binding->port == id && ports[i].owner == owner) {
            result = &ports[i]; break;
        }
    }
    k_spin_unlock(&registry_lock, key);
    return result;
}

int vport_register(const void *owner, const struct vport_binding *binding)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!owner || !binding || !binding->io || !binding->io->send ||
        !binding->io->mtu || !binding->receive) { return -EINVAL; }
    k_mutex_lock(&register_lock, K_FOREVER);
    struct port_state *slot = NULL;
    int err = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        if (ports[i].binding && ports[i].binding->port == binding->port) {
            err = ports[i].owner == owner && ports[i].binding == binding ? 0 : -EEXIST;
            goto out;
        }
        if (!ports[i].binding && !slot) { slot = &ports[i]; }
    }
    if (slot) {
        k_mutex_init(&slot->tx_lock);
        k_spinlock_key_t key = k_spin_lock(&registry_lock);
        slot->owner = owner;
        slot->binding = binding;
        k_spin_unlock(&registry_lock, key);
        err = 0;
    }
out:
    k_mutex_unlock(&register_lock);
    return err;
}

static int check(const struct port_state *port, const struct vport_source *source,
                  const struct vport_frame *frame)
{
    if (!source || !frame || !frame->data || !frame->size) { return -EINVAL; }
    return frame->size > port->binding->io->mtu ? -EMSGSIZE : 0;
}

int vport_input(const void *owner, uint16_t id, const struct vport_source *source,
                const struct vport_frame *frame)
{
    struct port_state *port = find(owner, id);
    if (!port) { return -ENOENT; }
    int err = check(port, source, frame);
    if (err) { return err; }
    err = port->binding->receive(port->binding->protocol, source, frame);
    return err > 0 ? -EIO : err;
}

int vport_output(const void *owner, uint16_t id, const struct vport_source *destination,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    struct port_state *port = find(owner, id);
    if (!port) { return -ENOENT; }
    if (!destination) { destination = &port->binding->io->destination; }
    int err = check(port, destination, frame);
    if (err) { return err; }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    err = k_mutex_lock(&port->tx_lock, sys_timepoint_timeout(deadline));
    if (err) { return err; }
    err = port->binding->io->send(port->binding->io->user, id, destination, frame,
                                  sys_timepoint_timeout(deadline));
    k_mutex_unlock(&port->tx_lock);
    return err > 0 ? -EIO : err;
}

void vport_complete(const void *owner, uint16_t id, const struct vport_source *source)
{
    struct port_state *port = find(owner, id);
    if (port && source && port->binding->io->complete) {
        port->binding->io->complete(port->binding->io->user, id, source);
    }
}
