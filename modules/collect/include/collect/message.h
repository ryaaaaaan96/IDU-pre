#ifndef CO_MESSAGE_H
#define CO_MESSAGE_H
#include <vport/message.h>
/* 端口注册/完整报文输入入口。receive 必须有界，耗时工作归协议队列。 */
int co_port_register(const struct vport_binding *binding);
int co_port_input(uint16_t port, const struct vport_source *source,
                       const struct vport_frame *frame);
int co_port_send(uint16_t port, const struct vport_source *destination,
                      const struct vport_frame *frame, k_timeout_t timeout);
void co_port_complete(uint16_t port, const struct vport_source *source);
#endif
