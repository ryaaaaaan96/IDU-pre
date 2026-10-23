#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <collect/modbus.h>
#endif
#include <collect/simulator.h>
#include <transmit/modbus_adapter.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <errno.h>

static int locate(struct co_simulator *sim, uint8_t fc, uint16_t address,
                   const struct co_simulator_register **entry)
{
    size_t offset = 0;
    for (size_t i = 0; i < sim->config->register_count; ++i) {
        const struct co_simulator_register *r = &sim->config->registers[i];
        if (fc == r->function && address >= r->address &&
            (uint32_t)address < (uint32_t)r->address + r->width) {
            if (entry) { *entry = r; }
            return offset + address - r->address;
        }
        offset += r->width;
    }
    return -ENOTSUP;
}

void co_simulator_set_online(struct co_simulator *sim, bool online)
{ if (sim) { atomic_set(&sim->online, online); } }
void co_simulator_set_write_fault(struct co_simulator *sim, enum co_simulator_fault fault)
{ if (sim) { atomic_set(&sim->fault, fault); } }
unsigned co_simulator_write_count(struct co_simulator *sim)
{ return sim ? atomic_get(&sim->writes) : 0; }

int co_simulator_set_register(struct co_simulator *sim, uint8_t fc,
                               uint16_t address, uint16_t value)
{
    if (!sim || !sim->initialized) { return -EACCES; }
    int slot = locate(sim, fc, address, NULL);
    if (slot < 0) { return slot; }
    k_mutex_lock(&sim->lock, K_FOREVER);
    sim->values[slot] = value;
    k_mutex_unlock(&sim->lock);
    return 0;
}

static int route(int iface, const struct modbus_adu *rx,
                  struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(iface);
    struct co_simulator *sim = user;
    if (rx->length < 4 || rx->length > sizeof(rx->data)) { return -EINVAL; }
    uint16_t address = sys_get_be16(rx->data);
    uint16_t count = rx->fc == 6 ? 1 : sys_get_be16(rx->data + 2);
    if (!count || (uint32_t)address + count > 65536) { return -EINVAL; }
    *tx = *rx;
    if (rx->fc == 3 || rx->fc == 4) {
        if (rx->length != 4 || count > 125 || 1 + count * 2 > sizeof(tx->data)) { return -EINVAL; }
        k_mutex_lock(&sim->lock, K_FOREVER);
        int err = 0;
        for (size_t i = 0; i < count; ++i) {
            int slot = locate(sim, rx->fc, address + i, NULL);
            if (slot < 0) { err = slot; break; }
            sys_put_be16(sim->values[slot], tx->data + 1 + 2 * i);
        }
        k_mutex_unlock(&sim->lock);
        tx->data[0] = count * 2;
        tx->length = 1 + count * 2;
        return err;
    }
    if (rx->fc != 6 && rx->fc != 16) { return -ENOTSUP; }
    if ((rx->fc == 6 && rx->length != 4) ||
        (rx->fc == 16 && (count > 123 || rx->length != 5 + 2 * count ||
                         rx->data[4] != count * 2))) { return -EINVAL; }
    if (atomic_get(&sim->fault) == CO_SIMULATOR_WRITE_REJECT) { return -EIO; }
    k_mutex_lock(&sim->lock, K_FOREVER);
    uint16_t *candidate = k_malloc(sizeof(sim->values));
    if (!candidate) { k_mutex_unlock(&sim->lock); return -ENOMEM; }
    memcpy(candidate, sim->values, sizeof(sim->values));
    int err = 0;
    for (size_t i = 0; i < count;) {
        const struct co_simulator_register *r;
        int slot = locate(sim, 3, address + i, &r);
        if (slot < 0 || !r->writable) { err = -ENOENT; break; }
        if (address + i != r->address || i + r->width > count) { err = -ERANGE; break; }
        uint32_t raw = 0;
        for (size_t w = 0; w < r->width; ++w) {
            uint16_t word = sys_get_be16(rx->data + (rx->fc == 6 ? 2 : 5) + 2 * (i + w));
            raw |= (uint32_t)word << (16 * w);
            candidate[slot + w] = word;
        }
        int64_t value = r->signed_value ? (r->width == 1 ? (int16_t)raw : (int32_t)raw) : (int64_t)raw;
        if (value < r->minimum || value > r->maximum) { err = -ERANGE; break; }
        i += r->width;
    }
    if (!err) {
        memcpy(sim->values, candidate, sizeof(sim->values));
        atomic_inc(&sim->writes);
    }
    k_free(candidate);
    k_mutex_unlock(&sim->lock);
    tx->length = 4;
    return err;
}

static int client_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    struct co_simulator *sim = user;
    return atomic_get(&sim->online) ? modbus_raw_submit_rx(sim->server, adu) : 0;
}

static int server_tx(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface);
    struct co_simulator *sim = user;
    if (adu->fc == 6 || adu->fc == 16) {
        int fault = atomic_get(&sim->fault);
        if (fault == CO_SIMULATOR_WRITE_DISCONNECT) { atomic_clear(&sim->online); }
        if (fault == CO_SIMULATOR_WRITE_DISCONNECT || fault == CO_SIMULATOR_WRITE_LOSE_ACK) { return 0; }
    }
    return modbus_raw_submit_rx(sim->client, adu);
}

int co_simulator_init(struct co_simulator *sim, const struct co_simulator_config *config)
{
    if (!sim || !config || !config->registers || !config->register_count ||
        !config->client_iface || !config->server_iface || !config->timeout_us) { return -EINVAL; }
    if (sim->started) { return sim->config == config ? sim->client : -EALREADY; }
    size_t words = 0;
    for (size_t i = 0; i < config->register_count; ++i) {
        const struct co_simulator_register *r = &config->registers[i];
        if ((r->function != 3 && r->function != 4) || !r->width || r->width > 2 ||
            (uint32_t)r->address + r->width > 65536 ||
            (r->writable && (r->function != 3 || r->minimum > r->maximum))) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            const struct co_simulator_register *p = &config->registers[j];
            if (r->function == p->function && r->address < (uint32_t)p->address + p->width &&
                p->address < (uint32_t)r->address + r->width) { return -EEXIST; }
        }
        words += r->width;
    }
    if (words > ARRAY_SIZE(sim->values)) { return -ENOSPC; }
    const struct dp_equip_cfg *equipment = dp_config();
    const struct dp_device_cfg *device = NULL;
    for (size_t i = 0; equipment && i < equipment->device_count; ++i) {
        if (equipment->devices[i].dev_id == config->dev_id) { device = &equipment->devices[i]; break; }
    }
    if (!device) { return -ENOENT; }
    int client = modbus_iface_get_by_name(config->client_iface);
    int server = modbus_iface_get_by_name(config->server_iface);
    if (client < 0 || server < 0 || client == server) { return -ENODEV; }
    sim->client = client; sim->server = server; sim->config = config;
    if (!sim->initialized) { k_mutex_init(&sim->lock); sim->initialized = true; }
    words = 0;
    for (size_t i = 0; i < config->register_count; ++i) {
        const struct co_simulator_register *r = &config->registers[i];
        uint32_t value = r->unit_address ? device->unit_id : r->initial;
        for (size_t w = 0; w < r->width; ++w) { sim->values[words++] = value >> (16 * w); }
    }
    atomic_set(&sim->online, 1);
    atomic_clear(&sim->fault);
    atomic_clear(&sim->writes);
    static struct modbus_user_callbacks callbacks;
    struct modbus_iface_param server_param = {
        .mode = MODBUS_MODE_RAW, .server = {.unit_id = device->unit_id, .user_cb = &callbacks},
        .rawcb = {.raw_tx_cb = server_tx, .user_data = sim},
    };
    /* RAW 接口在客户端启动前没有输入；先取得接口，避免失败时覆盖其他实例路由。 */
    int err = tr_modbus_init_server(server, server_param, NULL, false);
    if (!err) { err = tr_modbus_set_router(server, route, sim); }
    if (err) { return err; }
    struct modbus_iface_param client_param = {
        .mode = MODBUS_MODE_RAW, .rx_timeout = config->timeout_us,
        .rawcb = {.raw_tx_cb = client_tx, .user_data = sim},
    };
#ifdef CONFIG_CO_MODBUS_RAW_PORT
    err = co_modbus_port_param(client, &client_param);
    if (err) { modbus_disable(server); return err; }
#endif
    err = modbus_init_client(client, client_param);
    if (err) { modbus_disable(server); return err; }
    sim->started = true;
    return client;
}

#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <zephyr/sys/crc.h>
#include <transmit/message.h>
int co_simulator_port_send(struct co_simulator *sim, uint16_t destination,
                           const struct vport_source *source, const struct vport_frame *frame,
                           bool request)
{
    if (!sim || !sim->started) { return -EACCES; }
    if (request) {
        return atomic_get(&sim->online) ? tr_port_input(destination, source, frame) : 0;
    }
    if (frame->size >= 2 && (frame->data[1] == 6 || frame->data[1] == 16)) {
        int fault = atomic_get(&sim->fault);
        if (fault == CO_SIMULATOR_WRITE_DISCONNECT) { atomic_clear(&sim->online); }
        if (fault == CO_SIMULATOR_WRITE_DISCONNECT || fault == CO_SIMULATOR_WRITE_LOSE_ACK) { return 0; }
    }
    return co_port_input(destination, source, frame);
}
#endif
