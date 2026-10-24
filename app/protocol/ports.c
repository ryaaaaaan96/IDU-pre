#include "ports.h"
#include "signal_ids.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <collect/modbus.h>
#include <transmit/modbus.h>
#include <dataprocess/points.h>
#include <modbus_support/transport.h>
#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

/* 本机采集事务与转发响应共享一条 RS485；外部主站的发送仍需联调协调。 */
static int serial = -1, client = -1;
static K_MUTEX_DEFINE(wire_lock);
static K_MUTEX_DEFINE(ports_lock);
static struct k_spinlock response_lock;
static struct {
    bool active;
    k_tid_t owner;
    uint16_t dev_id;
    uint8_t unit, function;
    uint16_t address, value;
} expected;

static int send(void *user, uint16_t port, const struct vport_source *destination,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(user);
    if (destination->link != LINK_RS485 || destination->peer) { return -EACCES; }
    if (port == PORT_ACQUISITION) {
        /* collect_transfer 持有完整事务锁，不能只锁住一次 UART 发送。 */
        k_spinlock_key_t key = k_spin_lock(&response_lock);
        bool owned = expected.active && expected.owner == k_current_get() &&
                     expected.dev_id == destination->dev_id;
        k_spin_unlock(&response_lock, key);
        if (!owned) { return -EACCES; }
        return mb_serial_send(serial, frame->data, frame->size, timeout);
    }
    if (port != PORT_FORWARD) { return -ENOENT; }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    int err = k_mutex_lock(&wire_lock, sys_timepoint_timeout(deadline));
    if (err) { return err; }
    err = mb_serial_send(serial, frame->data, frame->size, sys_timepoint_timeout(deadline));
    k_mutex_unlock(&wire_lock);
    return err;
}

static const struct vport_io io = {
    .send = send,
    .complete = VPORT_IO_DEFAULT_COMPLETE,
    .user = VPORT_IO_DEFAULT_USER,
    .destination = {
        .link = LINK_RS485,
        .dev_id = VPORT_SOURCE_DEFAULT_DEVICE,
        .peer = VPORT_SOURCE_DEFAULT_PEER,
    },
    .mtu = CONFIG_MODBUS_BUFFER_SIZE,
};

static int collect_transfer(void *user, uint16_t dev_id, struct modbus_adu *adu,
                             k_timeout_t timeout)
{
    ARG_UNUSED(user);
    if (!adu || adu->length < 4) { return -EINVAL; }
    const DEV_TABLE_S *device = dp_dev_get_from_devid(dev_id);
    if (!device || device->port != protocol_collect_bus.id || device->coll_addr != adu->unit_id) {
        return -EINVAL;
    }
    /* 北向站号在本地初始化确定，后续 GPIO 样本不改变端口身份。 */
    if (!adu->unit_id || adu->unit_id == CONFIG_IDU_FORWARD_UNIT) { return -EADDRINUSE; }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    if (k_mutex_lock(&wire_lock, sys_timepoint_timeout(deadline))) { return -EBUSY; }
    k_spinlock_key_t key = k_spin_lock(&response_lock);
    expected.unit = adu->unit_id;
    expected.function = adu->fc;
    expected.address = sys_get_be16(adu->data);
    expected.value = sys_get_be16(adu->data + 2);
    expected.owner = k_current_get();
    expected.dev_id = dev_id;
    expected.active = true;
    k_spin_unlock(&response_lock, key);

    int err = co_modbus_transfer_timeout(client, dev_id, adu, sys_timepoint_timeout(deadline));

    key = k_spin_lock(&response_lock);
    expected.active = false; /* 超时、异常和发送失败也结束事务，不遗留收包归属。 */
    k_spin_unlock(&response_lock, key);
    k_mutex_unlock(&wire_lock);
    return err;
}

const struct co_bus_cfg protocol_collect_bus = {
    .id = 0,
    .iface = CO_BUS_DEFAULT_IFACE,
    .simulated = CO_BUS_DEFAULT_SIMULATED,
    .transfer = collect_transfer,
    .initialize = CO_BUS_DEFAULT_INITIALIZE,
    .read_model = CO_BUS_DEFAULT_READ_MODEL,
    .user = CO_BUS_DEFAULT_USER,
};
const struct co_port_config protocol_collect_port = {
    .iface_name = "RAW_0",
    .param = {
        .mode = MODBUS_MODE_RAW,
        .rx_timeout = CONFIG_CO_TIMEOUT_MS * 1000U,
        .rawcb = {
            .raw_tx_cb = CO_PORT_DEFAULT_RAW_TX,
            .user_data = CO_PORT_DEFAULT_RAW_USER,
        },
    },
    .simulator = CO_PORT_DEFAULT_SIMULATOR,
    .simulation = CO_PORT_DEFAULT_SIMULATION,
};

#if DT_HAS_ALIAS(idu_forward)
static int receive(void *user, const uint8_t *data, size_t size)
{
    ARG_UNUSED(user);
    struct vport_source source = {
        .link = LINK_RS485,
        .dev_id = VPORT_SOURCE_DEFAULT_DEVICE,
        .peer = VPORT_SOURCE_DEFAULT_PEER,
    };
    const struct vport_frame frame = {.data = data, .size = size};
    k_spinlock_key_t key = k_spin_lock(&response_lock);
    bool collecting = expected.active;
    if (collecting) { source.dev_id = expected.dev_id; }
    bool matches = size >= 5 && data[0] == expected.unit;
    if (matches && data[1] == (expected.function | 0x80U)) {
        matches = size == 5 && data[2] != 0;
    } else if (matches && data[1] == expected.function) {
        if (data[1] == 3 || data[1] == 4) {
            matches = size == 5U + expected.value * 2U && data[2] == expected.value * 2U;
        } else {
            matches = size == 8 && sys_get_be16(data + 2) == expected.address &&
                      sys_get_be16(data + 4) == expected.value;
        }
    } else { matches = false; }
    k_spin_unlock(&response_lock, key);
    int err = collecting ? (matches ? co_port_input(PORT_ACQUISITION, &source, &frame) : -ENOMSG) :
                           tr_port_input(PORT_FORWARD, &source, &frame);
    /* RAW 输入已复制报文。共享口必须立即恢复 RX，允许北向写处理期间接收南向回复。 */
    mb_serial_release(serial);
    return err;
}
#endif

int protocol_ports_init(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&ports_lock, K_FOREVER);
    int err = co_modbus_port_bind(PORT_ACQUISITION, "RAW_0", &io);
    if (!err) {
        client = modbus_iface_get_by_name("RAW_0");
        if (client < 0) { err = client; }
    }
#if DT_HAS_ALIAS(idu_forward)
    if (!err) { err = tr_modbus_port_bind(PORT_FORWARD, "RAW_1", &io); }
    if (!err && serial < 0) {
        struct modbus_serial_param param = {
            .baud = DT_PROP(DT_PARENT(DT_ALIAS(idu_forward)), current_speed),
            .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
        };
        int iface = mb_serial_open(DEVICE_DT_NAME(DT_ALIAS(idu_forward)), param, receive, NULL);
        if (iface < 0) { err = iface; } else { serial = iface; }
    }
#else
    if (!err) { err = -ENODEV; }
#endif
    k_mutex_unlock(&ports_lock);
    return err;
}
