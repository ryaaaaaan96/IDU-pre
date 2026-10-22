#include <transmit/forward.h>
#include <transmit/server.h>
#include <dataprocess/table_registry.h>
#include <dataprocess/points.h>
#if defined(CONFIG_TR_MODBUS_RAW_PORT)
#include <transmit/modbus.h>
#endif
#include <zephyr/kernel.h>

struct forwarding_state {
    const struct tr_forward_cfg *config;
    int iface;
    const MB_TABLE_CELL_T *rows;
    struct dp_equip_cfg view;
    struct tr_register_service registers;
    struct tr_server_config server;
};
static struct forwarding_state entries[CONFIG_TR_PORTS];
K_MUTEX_DEFINE(forwarding_lock);

static int read_mapping(const struct dp_equip_cfg *view, size_t index,
                         struct dp_transmit_cfg *map)
{
    const struct forwarding_state *entry = CONTAINER_OF(view, struct forwarding_state, view);
    const MB_TABLE_CELL_T *row = &entry->rows[index];
    return dp_transmit_from_reference(row, map);
}

static int attach(int iface, const struct tr_forward_cfg *config)
{
    if (!config || !config->forward_id || iface < 0 ||
        !dp_config() || k_is_in_isr()) { return -EINVAL; }
    struct forwarding_state *free = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(entries); ++i) {
        struct forwarding_state *entry = &entries[i];
        if (!entry->config) { if (!free) { free = entry; } continue; }
        if (entry->config == config && entry->iface == iface) {
            return tr_register_attach(iface, &entry->registers);
        }
        if (entry->iface == iface || entry->config->forward_id == config->forward_id ||
            entry->config->port_id == config->port_id) { return -EEXIST; }
    }
    if (!free) { return -ENOSPC; }
    const struct dp_registered_table *map, *segments = NULL;
    int err = dp_table_registry_find(config->transmit_table, PRO_MB_SLAVE, &map);
    if (err) { return err; }
    if (config->protocol_segments) {
        err = dp_table_registry_find(config->protocol_segments, PRO_ADDR_SLAVE, &segments);
        if (err) { return err; }
    }
    /* Build only a small reader view: all rows and points stay in Flash/DP. */
    free->iface = iface;
    free->rows = map->rows;
    free->view = *dp_config();
    free->view.transmit = NULL;
    free->view.transmit_count = map->count;
    free->view.transmit_get = read_mapping;
    free->view.exact_overlapping_reads = true;
    free->registers = (struct tr_register_service){.mapping = &free->view,
        .segments = segments ? segments->rows : NULL,
        .segment_count = segments ? segments->count : 0,
        .route_broadcast = config->route_broadcast};
    err = tr_register_attach(iface, &free->registers);
    if (err) { return err; }
    free->config = config;
    return 0;
}

int tr_forward_attach(int iface, const struct tr_forward_cfg *config)
{
    if (k_is_in_isr()) { return -EINVAL; }
    k_mutex_lock(&forwarding_lock, K_FOREVER);
    int err = attach(iface, config);
    k_mutex_unlock(&forwarding_lock);
    return err;
}

int tr_forward_service(const struct tr_forward_cfg *config, uint8_t unit)
{
#if defined(CONFIG_TR_MODBUS_RAW_PORT)
    if (!config || !unit || unit > 247 || k_is_in_isr()) { return -EINVAL; }
    const char *name = tr_modbus_port_name(config->port_id);
    if (!name) { return -ENOTCONN; }
    int iface = modbus_iface_get_by_name(name);
    k_mutex_lock(&forwarding_lock, K_FOREVER);
    int err = 0;
    bool attached = false;
    for (size_t i = 0; i < ARRAY_SIZE(entries); ++i) {
        if (entries[i].config != config) { continue; }
        if (entries[i].iface != iface) {
            k_mutex_unlock(&forwarding_lock);
            return -EEXIST;
        }
        attached = true;
        break;
    }
    if (!attached) { err = attach(iface, config); }
    if (err) { k_mutex_unlock(&forwarding_lock); return err; }
    for (size_t i = 0; i < ARRAY_SIZE(entries); ++i) {
        struct forwarding_state *entry = &entries[i];
        if (entry->config != config) { continue; }
        if (!entry->server.iface_name) {
            entry->server = (struct tr_server_config){.iface_name = name,
                .param = {.mode = MODBUS_MODE_RAW}, .write = tr_register_write,
                .attach = tr_register_attach, .protocol = &entry->registers,
                .recovery_timeout_ms = 20000};
        }
        const struct tr_server_config *server = &entry->server;
        k_mutex_unlock(&forwarding_lock);
        return tr_server_service(server, unit);
    }
    k_mutex_unlock(&forwarding_lock);
    return -ENOENT;
#else
    ARG_UNUSED(config); ARG_UNUSED(unit);
    return -ENOTSUP;
#endif
}
