#include "ports.h"
#include <collect/port.h>
#include <collect/modbus.h>
#include <transmit/modbus.h>
#include <modbus_io.h>
#include <zephyr/device.h>
#include <errno.h>
#ifdef CONFIG_IDU_COLLECT_VIRTUAL
#include <collect/simulator.h>
#endif

static int forward_serial = -1;
#ifdef CONFIG_IDU_COLLECT_RTU
static int collect_serial = -1;
#endif

/* 唯一的产品端口编排：模块只传完整报文及不透明来源标识。 */
static int send(void *user, uint16_t port, const struct vport_source *destination,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(user);
    switch (port) {
    case IDU_PORT_FORWARD:
        if (destination->link != IDU_LINK_RS485 || destination->peer) { return -EACCES; }
        return mb_serial_send(forward_serial, frame->data, frame->size, timeout);
    case IDU_PORT_ACQUISITION:
        if (destination->link != IDU_LINK_ACQUISITION || destination->peer) { return -EACCES; }
#ifdef CONFIG_IDU_COLLECT_VIRTUAL
        return co_simulator_port_send(&idu_collect_simulator, IDU_PORT_VIRTUAL_DEVICE,
                                       destination, frame, true);
#else
        return mb_serial_send(collect_serial, frame->data, frame->size, timeout);
#endif
#ifdef CONFIG_IDU_COLLECT_VIRTUAL
    case IDU_PORT_VIRTUAL_DEVICE:
        if (destination->link != IDU_LINK_ACQUISITION || destination->peer) { return -EACCES; }
        return co_simulator_port_send(&idu_collect_simulator, IDU_PORT_ACQUISITION,
                                       destination, frame, false);
#endif
    default: return -ENOENT;
    }
}

static void complete(void *user, uint16_t port, const struct vport_source *source)
{
    ARG_UNUSED(user);
    if (port == IDU_PORT_FORWARD && source->link == IDU_LINK_RS485) {
        mb_serial_release(forward_serial);
    }
#ifdef CONFIG_IDU_COLLECT_RTU
    if (port == IDU_PORT_ACQUISITION && source->link == IDU_LINK_ACQUISITION) {
        mb_serial_release(collect_serial);
    }
#endif
}

#if DT_HAS_ALIAS(idu_forward)
static const struct vport_io forward_io = {
    .send = send, .complete = complete, .mtu = CONFIG_MODBUS_BUFFER_SIZE,
    .destination = {.link = IDU_LINK_RS485},
};
#endif
static const struct vport_io collect_io = {
    .send = send, .complete = complete, .mtu = CONFIG_MODBUS_BUFFER_SIZE,
    .destination = {.link = IDU_LINK_ACQUISITION},
};

#if DT_HAS_ALIAS(idu_forward)
static int forward_receive(void *user, const uint8_t *data, size_t size)
{
    ARG_UNUSED(user);
    const struct vport_source source = {.link = IDU_LINK_RS485};
    const struct vport_frame frame = {.data = data, .size = size};
    return tr_port_input(IDU_PORT_FORWARD, &source, &frame);
}
#endif
#ifdef CONFIG_IDU_COLLECT_RTU
#define COLLECT_NODE DT_ALIAS(idu_collect)
BUILD_ASSERT(DT_NODE_HAS_STATUS(COLLECT_NODE, okay), "Configure idu-collect in devicetree");
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_PARENT(DT_ALIAS(idu_forward))),
             "Acquisition and forwarding need separate physical UARTs");
#if DT_HAS_CHOSEN(zephyr_console)
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_CHOSEN(zephyr_console)),
             "Acquisition must not take over the console UART");
#endif
static int collect_receive(void *user, const uint8_t *data, size_t size)
{
    ARG_UNUSED(user);
    const struct vport_source source = {.link = IDU_LINK_ACQUISITION};
    const struct vport_frame frame = {.data = data, .size = size};
    return co_port_input(IDU_PORT_ACQUISITION, &source, &frame);
}
const struct co_port_config idu_collect_port = {
    .iface_name = "RAW_0",
    .param = {.mode = MODBUS_MODE_RAW, .rx_timeout = CONFIG_CO_TIMEOUT_MS * 1000U},
};
#endif

int idu_forward_serial_configure(struct modbus_serial_param param)
{
    return forward_serial < 0 ? 0 : mb_serial_configure(forward_serial, param);
}

K_MUTEX_DEFINE(ports_lock);
int idu_ports_init(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&ports_lock, K_FOREVER);
    int err = co_modbus_port_bind(IDU_PORT_ACQUISITION, "RAW_0", &collect_io);
#ifdef CONFIG_IDU_COLLECT_VIRTUAL
    if (!err) { err = tr_modbus_port_bind(IDU_PORT_VIRTUAL_DEVICE, "RAW_1", &collect_io); }
#endif
#if DT_HAS_ALIAS(idu_forward)
    if (!err) { err = tr_modbus_port_bind(IDU_PORT_FORWARD, "RAW_2", &forward_io); }
    if (!err && forward_serial < 0) {
        struct modbus_serial_param param = {
            .baud = DT_PROP(DT_PARENT(DT_ALIAS(idu_forward)), current_speed),
            .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
        };
        int iface = mb_serial_open(DEVICE_DT_NAME(DT_ALIAS(idu_forward)), param, forward_receive, NULL);
        if (iface < 0) { err = iface; } else { forward_serial = iface; }
    }
#endif
#ifdef CONFIG_IDU_COLLECT_RTU
    if (!err && collect_serial < 0) {
        struct modbus_serial_param param = {
            .baud = DT_PROP(DT_PARENT(COLLECT_NODE), current_speed),
            .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
        };
        int iface = mb_serial_open(DEVICE_DT_NAME(COLLECT_NODE), param, collect_receive, NULL);
        if (iface < 0) { err = iface; } else { collect_serial = iface; }
    }
#endif
    k_mutex_unlock(&ports_lock);
    return err;
}
