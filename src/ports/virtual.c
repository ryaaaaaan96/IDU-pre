/* Virtual second serial link at the Zephyr Modbus ADU boundary. This exercises
 * the actual client/server stacks without claiming to emulate UART/RTU timing.
 */
#include "idu_equip.h"
#include "idu_pre.h"
#include <idu/modbus_adapter.h>
#include <string.h>
#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(idu_virtual, LOG_LEVEL_INF);

static int client_iface = -1;
static int server_iface = -1;
static atomic_t online = ATOMIC_INIT(1);
static uint16_t di = 0x0005;
static uint16_t currents[IDU_ANALOG_COUNT] = {100, 200, 300, 400, 500, 600, 700, 800};
static uint16_t protocol[2] = {IDU_PROTOCOL_VERSION & 0xffff, IDU_PROTOCOL_VERSION >> 16};
static uint16_t remote_address = 1;
static uint16_t parameters[3] = {100, 0, 0};
static atomic_t write_fault;
static atomic_t writes;

void idu_virtual_set_write_fault(enum idu_virtual_write_fault fault)
{
    atomic_set(&write_fault, fault);
}

unsigned idu_virtual_write_count(void)
{
    return atomic_get(&writes);
}
K_MUTEX_DEFINE(fixture_lock);

void idu_virtual_set_online(bool enabled)
{
    atomic_set(&online, enabled);
}

static uint16_t *fixture_register(uint8_t function, uint16_t address)
{
    if (function == 3 && address >= 40000 && address <= 40002) {
        return &parameters[address - 40000];
    }
    if (function == 3 && address == 30459) {
        return &remote_address;
    }
    if (function != 4) {
        return NULL;
    }
    if (address == 8152) {
        return &di;
    }
    if (address >= 8163 && address <= 8170) {
        return &currents[address - 8163];
    }
    if (address == 8150 || address == 8151) {
        return &protocol[address - 8150];
    }
    return NULL;
}

int idu_virtual_set_register(uint8_t function, uint16_t address, uint16_t value)
{
    uint16_t *reg = fixture_register(function, address);
    if (!reg) {
        return -ENOTSUP;
    }
    k_mutex_lock(&fixture_lock, K_FOREVER);
    *reg = value;
    k_mutex_unlock(&fixture_lock);
    return 0;
}

static int fixture_read(uint8_t function, uint16_t address, uint16_t *value)
{
    uint16_t *reg = fixture_register(function, address);
    if (!reg) {
        return -ENOTSUP;
    }
    k_mutex_lock(&fixture_lock, K_FOREVER);
    *value = *reg;
    k_mutex_unlock(&fixture_lock);
    return 0;
}

static int input_read(uint16_t address, uint16_t *value)
{
    return fixture_read(4, address, value);
}

static int holding_read(uint16_t address, uint16_t *value)
{
    return fixture_read(3, address, value);
}

static int holding_write(uint16_t address, const uint16_t *words, size_t count)
{
    if (address < 40000 || (uint32_t)address + count > 40003) {
        return -ENOENT;
    }
    if (atomic_get(&write_fault) == IDU_VIRTUAL_WRITE_REJECT) {
        return -EIO;
    }
    k_mutex_lock(&fixture_lock, K_FOREVER);
    uint16_t candidate[3];
    memcpy(candidate, parameters, sizeof(candidate));
    memcpy(candidate + address - 40000, words, count * sizeof(*words));
    int32_t offset = (int32_t)(candidate[1] | ((uint32_t)candidate[2] << 16));
    int err = 0;
    if (candidate[0] > 1000 || offset < -100000 || offset > 100000 ||
        address == 40002 || (address + count == 40002)) {
        err = -ERANGE;
    } else {
        memcpy(parameters, candidate, sizeof(parameters));
        atomic_inc(&writes);
    }
    k_mutex_unlock(&fixture_lock);
    return err;
}

static int client_tx(int iface, const struct modbus_adu *adu, void *user_data)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(user_data);
    return atomic_get(&online) ? modbus_raw_submit_rx(server_iface, adu) : 0;
}

static int server_tx(int iface, const struct modbus_adu *adu, void *user_data)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(user_data);
    if (adu->fc == 6 || adu->fc == 16) {
        int fault = atomic_get(&write_fault);
        if (fault == IDU_VIRTUAL_WRITE_DISCONNECT) {
            atomic_set(&online, false);
        }
        if (fault == IDU_VIRTUAL_WRITE_DISCONNECT || fault == IDU_VIRTUAL_WRITE_LOSE_ACK) {
            return 0;
        }
    }
    return modbus_raw_submit_rx(client_iface, adu);
}

int idu_virtual_port_init(void)
{
    static struct modbus_user_callbacks callbacks = {
        .input_reg_rd = input_read, .holding_reg_rd = holding_read,
    };
    client_iface = modbus_iface_get_by_name("RAW_0");
    server_iface = modbus_iface_get_by_name("RAW_1");
    if (client_iface < 0 || server_iface < 0) {
        return -ENODEV;
    }
    const struct idu_equip_cfg *cfg = idu_equipment_config();
    size_t bus_devices = 0;
    for (size_t i = 0; i < cfg->device_count; ++i) {
        if (cfg->devices[i].bus_id == 0) { ++bus_devices; }
    }
    if (bus_devices != 1 || cfg->devices[0].bus_id != 0) {
        return -ENOTSUP; /* This fixture models one downstream device. */
    }
    struct modbus_iface_param server = {
        .mode = MODBUS_MODE_RAW,
        .server = {.user_cb = &callbacks, .unit_id = cfg->devices[0].unit_id},
        .rawcb = {.raw_tx_cb = server_tx},
    };
    remote_address = cfg->devices[0].unit_id;
    int err = idu_modbus_init_server(server_iface, server, holding_write, false);
    if (err) {
        return err;
    }
    struct modbus_iface_param client = {
        .mode = MODBUS_MODE_RAW,
        .rx_timeout = CONFIG_IDU_COLLECT_TIMEOUT_MS * 1000U,
        .rawcb = {.raw_tx_cb = client_tx},
    };
    err = modbus_init_client(client_iface, client);
    if (err) {
        modbus_disable(server_iface);
        return err;
    }
    LOG_WRN("Acquisition is VIRTUAL: demo data, quality flag 0x0100");
    return client_iface;
}
