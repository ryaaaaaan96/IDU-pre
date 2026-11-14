/** @file @brief 共享协议服务和独立链路会话；APP 管理执行线程。 */
#ifndef TRANSMIT_H
#define TRANSMIT_H

#include <stddef.h>
#include <stdint.h>
#include <transmit/modbus.h>
#include <transmit/protocol.h>
#include <vport/vport.h>

typedef struct tr_engine tr_engine_t;
typedef struct tr_session tr_session_t;

typedef struct {
    vport_t *port;
    const tr_protocol_t *protocols;
    size_t protocol_count;
    uint32_t send_timeout_ms;
} tr_engine_config_t;

typedef struct {
    vport_channel_id_t channel_id;
    size_t rx_capacity;
    size_t tx_capacity;
    size_t max_frame;
    bool allow_routes;
    bool (*accept)(
        void *context, const vport_event_t *event); /**< NULL 全部接收；false 消费但不处理。 */
    void *accept_context;                           /**< 接收过滤上下文，须覆盖会话生命周期。 */
    void *custom_context; /**< 本链路 CUSTOM 可变状态，生命周期覆盖 session。 */
} tr_session_config_t;

/**
 * @brief INIT 创建；配置按值保存，协议及其引用的只读表必须常驻。
 * @note 生命周期由 APP 串行管理；不同 session 可并发输入，同 session 重入返回 -EBUSY。
 *       检查和执行回调禁止递归进入同 engine；发送在 engine 执行锁外。
 *       同 engine 共享站号、协议及收发时限；APP 校验各 UART 所需 gap 一致。
 *       执行锁无限等待，整帧超时不限制锁等待或业务回调耗时。
 */
int tr_engine_create(const tr_engine_config_t *config, tr_engine_t **out);
/** @brief INIT 分配独立缓存并绑定已挂载链路；失败不遗留绑定。 */
int tr_session_create(tr_engine_t *engine, const tr_session_config_t *config, tr_session_t **out);
/** @brief 仅 INIT/STOPPED，调用方先停止并等待所有访问；先解绑再释放。 */
int tr_session_destroy(tr_session_t *session);
/** @brief 仅 INIT/STOPPED，无 session 才允许关闭共享服务。 */
int tr_engine_destroy(tr_engine_t *engine);
#endif
