/** @file @brief 多通道同步事件路由；生命周期由调用方串行管理。 */
#ifndef VPORT_H
#define VPORT_H
#include <zephyr/kernel.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t vport_channel_id_t;

typedef enum {
    VPORT_DATA,
    VPORT_IDLE,
    VPORT_POLL,
    VPORT_ERROR,
} vport_event_type_t;

typedef enum {
    VPORT_INIT,
    VPORT_READY,
    VPORT_STOPPED,
} vport_state_t;

/** @brief 单调微秒接收时刻；DATA 为借用字节块，其余事件长度为零。 */
typedef struct {
    vport_event_type_t type;
    vport_channel_id_t channel_id;
    const uint8_t *data;
    size_t size;
    uint64_t at_us;
} vport_event_t;

/** @brief 返回 0 取得事件；-EAGAIN 无事件；-ECANCELED 已消费但主动丢弃；其他负值为错误。 */
typedef int (*vport_source_t)(void *context, vport_event_t *event);

typedef int (*vport_receive_t)(void *context, const vport_event_t *event);
typedef int (*vport_send_t)(void *context, const uint8_t *data, size_t size, k_timeout_t timeout);

typedef struct vport vport_t;

/** @brief APP 持有的常驻通道；next/owner 由模块维护，挂载后不修改字段。 */
typedef struct vport_channel {
    vport_channel_id_t channel_id;
    vport_send_t send;
    void *send_context;
    vport_receive_t receive;
    void *receive_context;
    vport_source_t source;
    void *source_context;
    atomic_t input_busy;    /**< 覆盖取事件至同步交付结束，由模块维护。 */
    atomic_t output_active; /**< 在途同步发送计数，由模块维护。 */
    struct vport_channel *next;
    vport_t *owner;
} vport_channel_t;

/** @brief 零初始化进入 INIT；READY 时集合只读，不支持运行期增删。 */
struct vport {
    vport_channel_t *channels;
    vport_state_t state;
};

/** @brief INIT 注册完整 send/source 通道；source 可为空，仅使用主动 input。 */
int vport_channel_add(vport_t *port, vport_channel_t *channel);
int vport_channel_bind(
    vport_t *port, vport_channel_id_t channel_id, vport_receive_t receive, void *context);
/** @brief READY 线程中取一个事件并同步交付；同通道重复或递归输入返回 -EBUSY。 */
int vport_poll(vport_t *port, vport_channel_id_t channel_id);
/** @brief 仅 INIT/STOPPED 可解绑；必须先停止并等待全部收发调用结束。 */
int vport_channel_unbind(vport_t *port, vport_channel_id_t channel_id);
/** @brief INIT/STOPPED 整体撤销注册并恢复 INIT；有绑定或在途收发返回 -EBUSY，不部分清理。
 * @note 调用方先停止并等待全部收发结束；不停止硬件，不释放节点或 context。
 */
int vport_reset(vport_t *port);
/** @brief 校验全部通道后 INIT -> READY；空集合不允许启动。 */
int vport_start(vport_t *port);
/** @brief 调用方先停止并等待全部收发结束，再执行 READY -> STOPPED。 */
int vport_stop(vport_t *port);
/** @brief READY 线程中同步交付；同通道 input/poll 须串行，竞争返回 -EBUSY；返回前消费或复制数据。
 */
int vport_input(vport_t *port, const vport_event_t *event);
/** @brief READY 同步发送；回调返回前完成或中止，禁止保留缓冲区指针。 */
int vport_output(vport_t *port,
    vport_channel_id_t channel_id,
    const uint8_t *data,
    size_t size,
    k_timeout_t timeout);
#endif
