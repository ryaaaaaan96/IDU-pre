/**
 * @file
 * @brief 转发角色的完整报文端口接口。
 */
#ifndef TR_MESSAGE_H
#define TR_MESSAGE_H
#include <vport/message.h>
/**
 * @brief 端口注册/完整报文输入入口。
 *
 * receive 必须有界，耗时工作归协议队列。
 */
int tr_port_register(const struct vport_binding *binding);
int tr_port_input(uint16_t port, const struct vport_source *source,
                       const struct vport_frame *frame);
int tr_port_send(uint16_t port, const struct vport_source *destination,
                      const struct vport_frame *frame, k_timeout_t timeout);
void tr_port_complete(uint16_t port, const struct vport_source *source);
#endif
