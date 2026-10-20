#include "communication/ports/config.h"
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

/* 当前接入的是 Zephyr 完成组帧的 Modbus 报文，不是未经组帧的 UART 字节流。
 * 新增 CAN/UART/网络后端时，在这三张表登记，不改 vport 核心。
 */
static const struct vport_link_config links[] = {
#if DT_HAS_ALIAS(idu_forward)
    {.id = IDU_LINK_RS485, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[FORWARD]},
#endif
    {.id = IDU_LINK_ACQUISITION, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[ACQUISITION]},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.id = IDU_LINK_VIRTUAL_DEVICE, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[VIRTUAL_DEVICE]},
#endif
};
static const struct vport_port_config ports[] = {
#if DT_HAS_ALIAS(idu_forward)
    {.id = IDU_PORT_FORWARD, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[FORWARD]},
#endif
    {.id = IDU_PORT_ACQUISITION, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[ACQUISITION]},
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    {.id = IDU_PORT_VIRTUAL_DEVICE, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[VIRTUAL_DEVICE]},
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
static bool bound, ready;

int idu_communication_ports_init(void)
{
    k_mutex_lock(&ports_lock, K_FOREVER);
    int err = 0;
    if (!ready) {
        err = vport_validate(&config);
        if (!err && !bound) {
            err = tr_modbus_transport_bind(endpoints, ARRAY_SIZE(endpoints));
            bound = !err;
        }
        if (!err) { err = vport_init(&config); }
        ready = !err;
    }
    k_mutex_unlock(&ports_lock);
    return err;
}
