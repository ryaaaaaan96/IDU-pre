#ifndef VPORT_H
#define VPORT_H
#include <vport/message.h>
/* 启动期注册常驻对象；owner 隔离 CO/TR。运行期不替换配置。 */
int vport_register(const void *owner, const struct vport_binding *binding);
int vport_input(const void *owner, uint16_t port, const struct vport_source *source,
                const struct vport_frame *frame);
int vport_output(const void *owner, uint16_t port, const struct vport_source *destination,
                 const struct vport_frame *frame, k_timeout_t timeout);
void vport_complete(const void *owner, uint16_t port, const struct vport_source *source);
#endif
