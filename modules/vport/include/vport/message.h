#ifndef VPORT_MESSAGE_H
#define VPORT_MESSAGE_H
#include <zephyr/kernel.h>
/* 完整报文及应用自定义链路/对端标识；不解释硬件类型或协议字段。 */
struct vport_frame { const uint8_t *data; size_t size; };
struct vport_source { uint16_t link; uint64_t peer; };
typedef int (*vport_send_fn)(void *user, uint16_t port,
                             const struct vport_source *destination,
                             const struct vport_frame *frame, k_timeout_t timeout);
typedef int (*vport_input_fn)(void *user, const struct vport_source *source,
                              const struct vport_frame *frame);
struct vport_io {
    vport_send_fn send;
    void (*complete)(void *user, uint16_t port, const struct vport_source *source);
    void *user;
    struct vport_source destination;
    size_t mtu;
};
struct vport_binding {
    uint16_t port;
    const struct vport_io *io;
    vport_input_fn receive;
    void *protocol;
};
/* send 在调用线程同步完成或终止发送；正数返回值视为错误。
 * receive 必须同步消费/复制报文，不保留 source/frame/data 指针。
 * 输入直接交给协议自己的队列；vport 不重复复制、不创建线程。
 * 实际链路共享、事务和硬件互斥由应用选择的发送实现管理。
 */
#endif
