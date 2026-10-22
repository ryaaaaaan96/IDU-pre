#ifndef TR_FORWARD_H
#define TR_FORWARD_H
#include <transmit/registers.h>

#define TR_FORWARD_DEFAULT_SEGMENTS 0U
#define TR_FORWARD_DEFAULT_BROADCAST false

/* ROM configuration. forward_id has its own namespace, distinct from DP keys.
 * port_id identifies a registered TR complete-message port, not a UART or unit.
 * Unit is supplied by the application during explicit initialization. */
struct tr_forward_cfg {
    uint16_t forward_id;
    uint16_t port_id;
    uint16_t transmit_table;
    uint16_t protocol_segments;
    bool route_broadcast;
};
/* Startup only: install on an explicit Modbus interface. Configuration and
 * registry stay immutable; a failed declaration never replaces an active one. */
int tr_forward_attach(int iface, const struct tr_forward_cfg *config);
int tr_forward_init(const struct tr_forward_cfg *config, uint8_t unit);
/* Periodic maintenance; does not attach or initialize a missing entry. */
int tr_forward_service(const struct tr_forward_cfg *config, uint8_t unit);
#endif
