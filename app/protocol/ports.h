/**
 * @file
 * @brief 产品物理通信链路的内部标识。
 */
#ifndef PROTOCOL_PORTS_H
#define PROTOCOL_PORTS_H

#include "inc/protocol.h"

/**
 * @brief 内部链路标识，不作为应用公共 API。
 */
typedef enum {
    PORT_FORWARD = 1, PORT_ACQUISITION = 2,
} communication_port_t;
typedef enum {
    LINK_RS485 = 1,
} communication_link_t;
#endif
