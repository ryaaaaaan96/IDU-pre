/** @file @brief 协议无关的端口输入输出；调用方保证同一端口串行。 */
#ifndef VPORT_H
#define VPORT_H
#include <zephyr/kernel.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VPORT_DATA,
    VPORT_IDLE,
    VPORT_POLL,
    VPORT_ERROR,
} vport_event_type_t;

/** @brief 接收时刻使用单调微秒；DATA 为借用字节块，其余事件长度为零。 */
typedef struct {
    vport_event_type_t type;
    const uint8_t *data;
    size_t size;
    uint64_t at_us;
} vport_event_t;

typedef int (*vport_receive_t)(void *context, const vport_event_t *event);
typedef int (*vport_send_t)(void *context, const uint8_t *data, size_t size, k_timeout_t timeout);

/** @brief APP 持有的常驻端口；receive 在启动绑定，运行期不替换。 */
typedef struct {
    vport_send_t send;
    void *send_context;
    vport_receive_t receive;
    void *receive_context;
} vport_t;

/** @brief 启动时绑定唯一接收者；不能覆盖活动绑定。 */
int vport_bind(vport_t *port, vport_receive_t receive, void *context);
/** @brief 线程中同步交付事件；返回前消费或复制，不能保留借用指针。 */
int vport_input(vport_t *port, const vport_event_t *event);
/** @brief APP 回调返回前完成或中止发送；禁止保留缓冲区指针。 */
int vport_output(vport_t *port, const uint8_t *data, size_t size, k_timeout_t timeout);
#endif
