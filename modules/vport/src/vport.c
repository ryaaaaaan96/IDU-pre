#include <vport/vport.h>
#include <errno.h>
#include <string.h>
#include <zephyr/sys/atomic.h>

#define CAN_FLAGS (VPORT_CAN_EXTENDED | VPORT_CAN_FD | VPORT_CAN_BRS)
BUILD_ASSERT(CONFIG_VPORT_THREAD_PRIORITY < CONFIG_NUM_PREEMPT_PRIORITIES,
             "VPORT_THREAD_PRIORITY must be a valid preemptible priority");

struct link_state {
    struct k_mutex tx_lock;
    struct vport_stats stats;
};
struct rx_item {
    void *fifo_reserved;
    struct vport_source source;
    size_t size;
    uint32_t id;
    uint8_t flags;
    uint8_t data[CONFIG_VPORT_MAX_PAYLOAD];
};

static const struct vport_config *configuration;
static struct link_state links[CONFIG_VPORT_MAX_LINKS];
static uint32_t sequences[CONFIG_VPORT_MAX_ROUTES];
static struct k_spinlock state_lock;
static atomic_t ready;
K_MUTEX_DEFINE(init_lock);
K_FIFO_DEFINE(rx_queue);
K_MEM_SLAB_DEFINE(rx_pool, sizeof(struct rx_item), CONFIG_VPORT_RX_DEPTH,
                  __alignof__(struct rx_item));
K_THREAD_STACK_DEFINE(rx_stack, CONFIG_VPORT_STACK_SIZE);
static struct k_thread rx_thread;

static int link_index(const struct vport_config *cfg, uint16_t id)
{
    for (size_t i = 0; i < cfg->link_count; ++i) {
        if (cfg->links[i].id == id) { return (int)i; }
    }
    return -ENOENT;
}

static int port_index(const struct vport_config *cfg, uint16_t id)
{
    for (size_t i = 0; i < cfg->port_count; ++i) {
        if (cfg->ports[i].id == id) { return (int)i; }
    }
    return -ENOENT;
}

static bool valid_type(enum vport_type type)
{
    return type == VPORT_STREAM || type == VPORT_DATAGRAM || type == VPORT_CAN;
}

static bool overlap(const struct vport_route *a, const struct vport_route *b)
{
    if (a->match_peer && b->match_peer && a->peer != b->peer) { return false; }
    return !((a->rx_id ^ b->rx_id) & a->rx_mask & b->rx_mask) &&
           !((a->rx_flags ^ b->rx_flags) & a->rx_flags_mask & b->rx_flags_mask);
}

int vport_validate(const struct vport_config *cfg)
{
    if (!cfg || !cfg->links || !cfg->ports || !cfg->routes ||
        !cfg->link_count || !cfg->port_count || !cfg->route_count) { return -EINVAL; }
    if (cfg->link_count > CONFIG_VPORT_MAX_LINKS ||
        cfg->port_count > CONFIG_VPORT_MAX_PORTS ||
        cfg->route_count > CONFIG_VPORT_MAX_ROUTES) { return -E2BIG; }
    for (size_t i = 0; i < cfg->link_count; ++i) {
        const struct vport_link_config *link = &cfg->links[i];
        if (!valid_type(link->type) || !link->mtu ||
            link->mtu > CONFIG_VPORT_MAX_PAYLOAD || !link->ops || !link->ops->send) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (cfg->links[j].id == link->id) { return -EEXIST; }
        }
    }
    for (size_t i = 0; i < cfg->port_count; ++i) {
        if (!valid_type(cfg->ports[i].type)) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            if (cfg->ports[j].id == cfg->ports[i].id) { return -EEXIST; }
        }
    }
    for (size_t i = 0; i < cfg->route_count; ++i) {
        const struct vport_route *route = &cfg->routes[i];
        int l = link_index(cfg, route->link), p = port_index(cfg, route->port);
        if (l < 0 || p < 0) { return -ENOENT; }
        if (!route->directions || (route->directions & ~(VPORT_RX | VPORT_TX)) ||
            ((route->directions & VPORT_RX) && !cfg->ports[p].receive)) { return -EINVAL; }
        if (cfg->links[l].type != cfg->ports[p].type) { return -EPROTOTYPE; }
        if ((route->rx_id | route->rx_mask) > 0x1fffffffU ||
            ((route->rx_flags | route->rx_flags_mask) & ~CAN_FLAGS) ||
            (route->rx_id & ~route->rx_mask) ||
            (route->rx_flags & ~route->rx_flags_mask)) { return -EINVAL; }
        if (cfg->links[l].type != VPORT_CAN &&
            (route->rx_id || route->rx_mask || route->rx_flags || route->rx_flags_mask)) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            const struct vport_route *other = &cfg->routes[j];
            if (route->link != other->link) { continue; }
            if ((route->directions & other->directions & VPORT_RX) &&
                overlap(route, other)) { return -EADDRINUSE; }
        }
    }
    return 0;
}

static void receive_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        struct rx_item *item = k_fifo_get(&rx_queue, K_FOREVER);
        int p = port_index(configuration, item->source.port);
        int l = link_index(configuration, item->source.link);
        const struct vport_port_config *port = &configuration->ports[p];
        const struct vport_frame frame = {
            .data = item->data, .size = item->size, .id = item->id, .flags = item->flags,
        };
        port->receive(&item->source, &frame, port->user);
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        ++links[l].stats.rx_delivered;
        k_spin_unlock(&state_lock, key);
        k_mem_slab_free(&rx_pool, item);
    }
}

int vport_init(const struct vport_config *cfg)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&init_lock, K_FOREVER);
    int err = atomic_get(&ready) ? -EALREADY : vport_validate(cfg);
    if (!err) {
        for (size_t i = 0; i < cfg->link_count; ++i) { k_mutex_init(&links[i].tx_lock); }
        configuration = cfg;
        atomic_set(&ready, 1);
        k_thread_create(&rx_thread, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
                        receive_worker, NULL, NULL, NULL,
                        CONFIG_VPORT_THREAD_PRIORITY, 0, K_NO_WAIT);
    }
    k_mutex_unlock(&init_lock);
    return err;
}

static int check_frame(const struct vport_link_config *link, const struct vport_frame *frame)
{
    if (!frame || (frame->size && !frame->data)) { return -EINVAL; }
    if (frame->size > link->mtu) { return -EMSGSIZE; }
    if (link->type != VPORT_CAN) {
        return frame->id || frame->flags ? -EINVAL : 0;
    }
    if ((frame->flags & ~CAN_FLAGS) ||
        frame->id > ((frame->flags & VPORT_CAN_EXTENDED) ? 0x1fffffffU : 0x7ffU) ||
        ((frame->flags & VPORT_CAN_BRS) && !(frame->flags & VPORT_CAN_FD))) { return -EINVAL; }
    if (!(frame->flags & VPORT_CAN_FD)) { return frame->size <= 8 ? 0 : -EMSGSIZE; }
    switch (frame->size) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 12: case 16: case 20: case 24: case 32: case 48: case 64:
        return 0;
    default: return -EMSGSIZE;
    }
}

static bool peer_matches(const struct vport_route *route, uint64_t peer)
{
    return !route->match_peer || route->peer == peer;
}

int vport_rx_gap(uint16_t id, uint64_t peer)
{
    if (!atomic_get(&ready)) { return -EACCES; }
    int l = link_index(configuration, id);
    if (l < 0) { return l; }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    for (size_t i = 0; i < configuration->route_count; ++i) {
        const struct vport_route *route = &configuration->routes[i];
        if (route->link == id && (route->directions & VPORT_RX) && peer_matches(route, peer)) {
            ++sequences[i];
        }
    }
    ++links[l].stats.rx_dropped;
    k_spin_unlock(&state_lock, key);
    return 0;
}

int vport_rx_submit(uint16_t id, uint64_t peer, const struct vport_frame *frame)
{
    if (!atomic_get(&ready)) { return -EACCES; }
    int l = link_index(configuration, id);
    if (l < 0) { return l; }
    int err = check_frame(&configuration->links[l], frame);
    if (err) {
        if (err == -EMSGSIZE) { vport_rx_gap(id, peer); }
        return err;
    }
    size_t r;
    for (r = 0; r < configuration->route_count; ++r) {
        const struct vport_route *route = &configuration->routes[r];
        if (route->link == id && (route->directions & VPORT_RX) && peer_matches(route, peer) &&
            (frame->id & route->rx_mask) == route->rx_id &&
            (frame->flags & route->rx_flags_mask) == route->rx_flags) { break; }
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (r == configuration->route_count) {
        ++links[l].stats.rx_unrouted;
        k_spin_unlock(&state_lock, key);
        return -ENOENT;
    }
    uint32_t sequence = ++sequences[r];
    k_spin_unlock(&state_lock, key);
    struct rx_item *item;
    if (k_mem_slab_alloc(&rx_pool, (void **)&item, K_NO_WAIT)) {
        key = k_spin_lock(&state_lock);
        ++links[l].stats.rx_dropped;
        k_spin_unlock(&state_lock, key);
        return -EAGAIN;
    }
    item->source = (struct vport_source){
        .port = configuration->routes[r].port, .link = id, .peer = peer, .sequence = sequence,
    };
    item->size = frame->size; item->id = frame->id; item->flags = frame->flags;
    if (frame->size) { memcpy(item->data, frame->data, frame->size); }
    key = k_spin_lock(&state_lock);
    ++links[l].stats.rx_queued;
    k_spin_unlock(&state_lock, key);
    k_fifo_put(&rx_queue, item);
    return 0;
}

static int tx_link(uint16_t port, uint16_t link, uint64_t peer)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!atomic_get(&ready)) { return -EACCES; }
    int l = link_index(configuration, link);
    if (l < 0 || port_index(configuration, port) < 0) { return -ENOENT; }
    for (size_t i = 0; i < configuration->route_count; ++i) {
        const struct vport_route *r = &configuration->routes[i];
        if (r->link == link && r->port == port && (r->directions & VPORT_TX) &&
            peer_matches(r, peer)) { return l; }
    }
    return -EACCES;
}

int vport_claim(uint16_t port, uint16_t link, uint64_t peer, k_timeout_t timeout)
{
    int l = tx_link(port, link, peer);
    return l < 0 ? l : k_mutex_lock(&links[l].tx_lock, timeout);
}

int vport_release(uint16_t link)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!atomic_get(&ready)) { return -EACCES; }
    int l = link_index(configuration, link);
    return l < 0 ? l : k_mutex_unlock(&links[l].tx_lock);
}

int vport_send(uint16_t port, uint16_t link, uint64_t peer,
               const struct vport_frame *frame, k_timeout_t timeout)
{
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    int l = tx_link(port, link, peer);
    if (l < 0) { return l; }
    const struct vport_link_config *cfg = &configuration->links[l];
    int err = check_frame(cfg, frame);
    if (err) { return err; }
    err = k_mutex_lock(&links[l].tx_lock, sys_timepoint_timeout(deadline));
    if (err) { return err; }
    err = cfg->ops->send(cfg->context, peer, frame, sys_timepoint_timeout(deadline));
    if (err > 0) { err = -EIO; }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (err) { ++links[l].stats.tx_errors; }
    else { ++links[l].stats.tx_completed; }
    k_spin_unlock(&state_lock, key);
    k_mutex_unlock(&links[l].tx_lock);
    return err;
}

int vport_reply(const struct vport_source *source, const struct vport_frame *frame,
                k_timeout_t timeout)
{
    if (!source) { return -EINVAL; }
    return vport_send(source->port, source->link, source->peer, frame, timeout);
}

int vport_get_stats(uint16_t id, struct vport_stats *stats)
{
    if (!stats) { return -EINVAL; }
    if (!atomic_get(&ready)) { return -EACCES; }
    int l = link_index(configuration, id);
    if (l < 0) { return l; }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    *stats = links[l].stats;
    k_spin_unlock(&state_lock, key);
    return 0;
}
