#include "protocol/ports.h"
#include "simulation.h"
#include <collect/modbus.h>
#include <collect/simulator.h>
#include <transmit/modbus.h>
#include <errno.h>

typedef enum { SIMULATED_DEVICE = 3 } simulated_device_t;
static int send(void *user, uint16_t port, const struct vport_source *source,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(user); ARG_UNUSED(timeout);
    return co_simulator_port_send(&idu_collect_simulator,
        port == PORT_ACQUISITION ? SIMULATED_DEVICE : PORT_ACQUISITION,
        source, frame, port == PORT_ACQUISITION);
}
static const struct vport_io io = {
    .send = send, .mtu = CONFIG_MODBUS_BUFFER_SIZE, .destination = {.link = LINK_RS485},
};
int protocol_ports_init(void)
{
    int err = co_modbus_port_bind(PORT_ACQUISITION, "RAW_0", &io);
    return err ? err : tr_modbus_port_bind(SIMULATED_DEVICE, "RAW_1", &io);
}
