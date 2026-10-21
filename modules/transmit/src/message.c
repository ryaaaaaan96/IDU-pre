#include <transmit/message.h>
#include <vport/vport.h>
static const char owner;
int tr_port_register(const struct vport_binding *binding)
{ return vport_register(&owner, binding); }
int tr_port_input(uint16_t port, const struct vport_source *source,
                       const struct vport_frame *frame)
{ return vport_input(&owner, port, source, frame); }
int tr_port_send(uint16_t port, const struct vport_source *destination,
                      const struct vport_frame *frame, k_timeout_t timeout)
{ return vport_output(&owner, port, destination, frame, timeout); }
void tr_port_complete(uint16_t port, const struct vport_source *source)
{ vport_complete(&owner, port, source); }
