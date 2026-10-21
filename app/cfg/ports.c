#include "ports.h"
#include <errno.h>
#include <collect/port.h>
#include <transmit/modbus_transport.h>
#include <zephyr/device.h>

enum endpoint_index {
#if DT_HAS_ALIAS(idu_forward)
    FORWARD,
#endif
    ACQUISITION,
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    VIRTUAL_DEVICE,
#endif
};

static const struct tr_modbus_transport endpoints[] = {
#if DT_HAS_ALIAS(idu_forward)
    [FORWARD] = {
        .iface_name = DEVICE_DT_NAME(DT_ALIAS(idu_forward)),
        .port = IDU_PORT_FORWARD, .link = IDU_LINK_RS485,
    },
#endif
    [ACQUISITION] = {
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
        .iface_name = "RAW_0",
#else
        .iface_name = DEVICE_DT_NAME(DT_ALIAS(idu_collect)),
#endif
        .port = IDU_PORT_ACQUISITION, .link = IDU_LINK_ACQUISITION,
    },
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    [VIRTUAL_DEVICE] = {
        .iface_name = "RAW_1",
        .port = IDU_PORT_VIRTUAL_DEVICE, .link = IDU_LINK_VIRTUAL_DEVICE,
    },
#endif
};

/* 应用决定虚拟口最终使用哪个硬件/仿真接口；模块不读取产品端口编号。 */
static const struct tr_modbus_transport *route_endpoint(uint16_t port, uint16_t link)
{
    for (size_t i = 0; i < ARRAY_SIZE(endpoints); ++i) {
        if (endpoints[i].port == port && endpoints[i].link == link) {
            return &endpoints[i];
        }
    }
    return NULL;
}

static int route_send(void *context, uint16_t port, uint64_t peer,
                       const struct vport_frame *frame, k_timeout_t timeout)
{
    const uint16_t *link = context;
    const struct tr_modbus_transport *endpoint = route_endpoint(port, *link);

    if (!endpoint) {
        return -EACCES;
    }
    /* 适配器调用实际 UART/RAW 接口，保留 Zephyr 的 CRC、帧间隔及 DE 控制。
     * 此回调已经持有所选 link 的锁，不在这里跨链路隐式转发或广播。
     */
    return tr_modbus_transport_send(endpoint, port, peer, frame, timeout);
}

static void route_receive(const struct vport_source *source,
                           const struct vport_frame *frame, void *user)
{
    ARG_UNUSED(user);
    const struct tr_modbus_transport *endpoint = route_endpoint(source->port, source->link);

    if (endpoint) {
        /* 保留虚拟口、物理链路和对端，交给对应的采集/转发协议上下文。 */
        tr_modbus_transport_receive(endpoint, source, frame);
    }
}

static const struct vport_backend_ops route_ops = {.send = route_send};

/* 当前接入的是 Zephyr 完成组帧的 Modbus 报文，不是未经组帧的 UART 字节流。
 * 新增 CAN/UART/网络后端时，在这三张表登记，不改 vport 核心。
 */
static const struct vport_link_config links[] = {
#if DT_HAS_ALIAS(idu_forward)
    {.id = IDU_LINK_RS485, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &route_ops, .context = (void *)&endpoints[FORWARD].link},
#endif
    {.id = IDU_LINK_ACQUISITION, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &route_ops, .context = (void *)&endpoints[ACQUISITION].link},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.id = IDU_LINK_VIRTUAL_DEVICE, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &route_ops, .context = (void *)&endpoints[VIRTUAL_DEVICE].link},
#endif
};
static const struct vport_port_config ports[] = {
#if DT_HAS_ALIAS(idu_forward)
    {.id = IDU_PORT_FORWARD, .type = VPORT_DATAGRAM, .receive = route_receive},
#endif
    {.id = IDU_PORT_ACQUISITION, .type = VPORT_DATAGRAM, .receive = route_receive},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.id = IDU_PORT_VIRTUAL_DEVICE, .type = VPORT_DATAGRAM, .receive = route_receive},
#endif
};
static const struct vport_route routes[] = {
#if DT_HAS_ALIAS(idu_forward)
    {.port = IDU_PORT_FORWARD, .link = IDU_LINK_RS485,
     .directions = VPORT_RX | VPORT_TX, .match_peer = true, .peer = 0},
#endif
    {.port = IDU_PORT_ACQUISITION, .link = IDU_LINK_ACQUISITION,
     .directions = VPORT_RX | VPORT_TX, .match_peer = true, .peer = 0},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.port = IDU_PORT_VIRTUAL_DEVICE, .link = IDU_LINK_VIRTUAL_DEVICE,
     .directions = VPORT_RX | VPORT_TX, .match_peer = true, .peer = 0},
#endif
};
static const struct vport_config config = {
    .links = links, .link_count = ARRAY_SIZE(links),
    .ports = ports, .port_count = ARRAY_SIZE(ports),
    .routes = routes, .route_count = ARRAY_SIZE(routes),
};
K_MUTEX_DEFINE(ports_lock);
static bool endpoints_bound;
static bool ports_ready;

int idu_ports_init(void)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&ports_lock, K_FOREVER);
    int err = 0;

    if (!ports_ready) {
        /* 先检查路由，再绑定适配器，最后发布回调；完成后才允许启动收发。 */
        err = vport_validate(&config);
        if (!err && !endpoints_bound) {
            err = tr_modbus_transport_bind(endpoints, ARRAY_SIZE(endpoints));
            endpoints_bound = !err;
        }
        if (!err) {
            err = vport_init(&config);
        }
        ports_ready = !err;
    }
    k_mutex_unlock(&ports_lock);
    return err;
}

#if defined(CONFIG_IDU_COLLECT_RTU)
#define COLLECT_NODE DT_ALIAS(idu_collect)
BUILD_ASSERT(DT_NODE_HAS_STATUS(COLLECT_NODE, okay), "Configure idu-collect in devicetree");
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_PARENT(DT_ALIAS(idu_forward))),
             "Acquisition and forwarding need separate physical UARTs");
#if DT_HAS_CHOSEN(zephyr_console)
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_CHOSEN(zephyr_console)),
             "Acquisition must not take over the console UART");
#endif
const struct co_port_config idu_collect_port = {
    .iface_name = DEVICE_DT_NAME(COLLECT_NODE),
    .param = {
        .mode = MODBUS_MODE_RTU, .rx_timeout = CONFIG_CO_TIMEOUT_MS * 1000U,
        .serial = {.baud = DT_PROP(DT_PARENT(COLLECT_NODE), current_speed),
                   .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1},
    },
};
#endif
