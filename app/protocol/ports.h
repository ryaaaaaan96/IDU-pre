#ifndef IDU_PROTOCOL_PORTS_H
#define IDU_PROTOCOL_PORTS_H

#include "inc/protocol.h"

/* 内部链路标识，不作为应用公共 API。 */
enum idu_communication_port {
    IDU_PORT_FORWARD = 1, IDU_PORT_ACQUISITION = 2,
};
enum idu_communication_link {
    IDU_LINK_RS485 = 1,
};
#endif
