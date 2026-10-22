#ifdef CONFIG_TR_MODBUS_RAW_PORT
#include <transmit/modbus.h>
#endif
#include <transmit/server.h>
#include <transmit/transmit.h>
#include <zephyr/kernel.h>
#include <limits.h>

struct server_state {
    const struct tr_server_config *config;
    int iface;
    bool configured, changing, attached, initialized;
    struct modbus_iface_param active, pending;
};
static struct server_state servers[CONFIG_TR_PORTS];
K_MUTEX_DEFINE(server_lock);

static bool same_settings(const struct modbus_iface_param *a,
                           const struct modbus_iface_param *b)
{
    if (a->mode != b->mode || a->server.unit_id != b->server.unit_id ||
        a->server.user_cb != b->server.user_cb) { return false; }
    if (a->mode == MODBUS_MODE_RAW) {
        return a->rawcb.raw_tx_cb == b->rawcb.raw_tx_cb && a->rawcb.user_data == b->rawcb.user_data;
    }
    return a->serial.baud == b->serial.baud && a->serial.parity == b->serial.parity &&
           a->serial.stop_bits == b->serial.stop_bits;
}

static void changed(int iface, int error, void *user)
{
    struct server_state *server = user;
    ARG_UNUSED(iface);
    k_mutex_lock(&server_lock, K_FOREVER);
    if (!error) { server->active = server->pending; }
    if (error == -ENOTRECOVERABLE) { server->configured = false; }
    server->changing = false;
    k_mutex_unlock(&server_lock);
}

static int server_service(const struct tr_server_config *config, uint8_t unit, bool initialize)
{
    if (!config || !config->iface_name || !unit || unit > 247 || k_is_in_isr() ||
        !config->recovery_timeout_ms || config->recovery_timeout_ms > INT32_MAX) {
        return -EINVAL;
    }
    k_mutex_lock(&server_lock, K_FOREVER);
    struct server_state *server = NULL, *free = NULL;
    int iface = modbus_iface_get_by_name(config->iface_name);
    int err = iface < 0 ? iface : 0;
    for (size_t i = 0; !err && i < ARRAY_SIZE(servers); ++i) {
        if (servers[i].config == config) { server = &servers[i]; break; }
        if (!servers[i].config) { if (!free) { free = &servers[i]; } }
        else if (servers[i].iface == iface) { err = -EEXIST; }
    }
    if (!err && !server) {
        if (!initialize) { err = -ENODEV; }
        else if (!free) { err = -ENOSPC; }
        else { server = free; server->config = config; server->iface = iface; }
    }
    if (!err && !initialize && !server->initialized) { err = -ENODEV; }
    if (!err && initialize && server->initialized) { err = -EALREADY; }
    if (!err && !server->attached) {
        err = config->attach ? config->attach(iface, config->protocol) : tr_attach(iface, NULL);
        server->attached = !err;
    }
    struct modbus_iface_param param = config->param;
    param.server.unit_id = unit;
    if (!param.server.user_cb) { param.server.user_cb = tr_callbacks(); }
    if (!err && config->configure) { err = config->configure(&param); }
    if (err == -EBUSY) { k_mutex_unlock(&server_lock); return initialize ? err : 0; }
#ifdef CONFIG_TR_MODBUS_RAW_PORT
    if (!err) { err = tr_modbus_port_param(iface, &param); }
#endif
    if (!err && (!param.server.unit_id || param.server.unit_id > 247)) { err = -ERANGE; }
    if (!err && !server->configured) {
        err = tr_modbus_init_server(iface, param, config->write, true);
        if (!err) { server->active = param; server->configured = true; server->initialized = true; }
    } else if (!err && !server->changing && !same_settings(&server->active, &param)) {
        server->pending = param;
        err = tr_modbus_reconfigure(iface, param, changed, server);
        server->changing = !err;
        if (err == -EBUSY) { err = 0; }
    }
    k_mutex_unlock(&server_lock);
    return err || initialize ? err : tr_modbus_service(iface, config->recovery_timeout_ms);
}

int tr_server_init(const struct tr_server_config *config, uint8_t unit)
{
    return server_service(config, unit, true);
}

int tr_server_service(const struct tr_server_config *config, uint8_t unit)
{
    return server_service(config, unit, false);
}
