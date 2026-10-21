#ifndef VPORT_H
#define VPORT_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* 类型描述数据边界；UART/RS485/TCP 为流，UDP 为报文，CAN 保留帧元数据。
 * 同一逻辑端口只能绑定相同类型的链路；不同链路的流不能直接拼接。
 */
enum vport_type { VPORT_STREAM, VPORT_DATAGRAM, VPORT_CAN };

enum vport_can_flags {
    VPORT_CAN_EXTENDED = BIT(0),
    VPORT_CAN_FD = BIT(1),
    VPORT_CAN_BRS = BIT(2),
};
enum vport_direction { VPORT_RX = BIT(0), VPORT_TX = BIT(1) };

struct vport_frame {
    const uint8_t *data;
    size_t size;
    uint32_t id;        /* CAN ID；其他类型必须为零。 */
    uint8_t flags;      /* vport_can_flags；当前只支持 CAN 数据帧。 */
};

struct vport_source {
    uint16_t port;
    uint16_t link;
    uint64_t peer;      /* 后端的不透明对端/连接令牌，可编码连接代次。 */
    uint32_t sequence;  /* 每条 RX 路由递增，包含队列满时丢失的输入。 */
};

/* 在共享 RX 线程调用，不持有发送锁。source/frame/data 只在回调期间有效。
 * 回调应有界，不等待后续 RX；耗时协议处理需复制到自己的队列。
 */
typedef void (*vport_receive_fn)(const struct vport_source *source,
                                const struct vport_frame *frame, void *user);

struct vport_backend_ops {
    /* port 是发起发送的虚拟端口，context 标识已选中的物理链路。
     * 应用可据此编排硬件调用；不能绕过该链路的锁发送到其他链路。
     * 同步发送：0 成功，负 errno 失败；不能保留 frame/data 指针。
     * timeout 是取得链路锁后的剩余预算。返回前完成发送或妥善终止/隔离
     * 未完成操作；不能仅提交异步驱动后立即返回成功。
     * 超时不保证远端未收到数据。核心不会自动重试、广播或切换链路。
     */
    int (*send)(void *context, uint16_t port, uint64_t peer, const struct vport_frame *frame,
                k_timeout_t timeout);
};

struct vport_link_config {
    uint16_t id;
    enum vport_type type;
    size_t mtu;        /* 单次输入/输出上限，不能超过 VPORT_MAX_PAYLOAD。 */
    const struct vport_backend_ops *ops;
    void *context;
};

struct vport_port_config {
    uint16_t id;
    enum vport_type type;
    vport_receive_fn receive; /* 仅发送端口可为 NULL。 */
    void *user;
};

struct vport_route {
    uint16_t port;
    uint16_t link;
    uint8_t directions;
    bool match_peer;
    uint64_t peer;         /* match_peer=true 时精确匹配，RX/TX 均适用。 */
    uint32_t rx_id;
    uint32_t rx_mask;      /* CAN 接收 ID 掩码；非 CAN 类型必须为零。 */
    uint8_t rx_flags;
    uint8_t rx_flags_mask; /* 用 EXTENDED 区分数值相同的标准帧和扩展帧。 */
};

struct vport_config {
    const struct vport_link_config *links;
    size_t link_count;
    const struct vport_port_config *ports;
    size_t port_count;
    const struct vport_route *routes;
    size_t route_count;
};

struct vport_stats {
    uint32_t rx_queued;
    uint32_t rx_delivered;
    uint32_t rx_unrouted;
    uint32_t rx_dropped;
    uint32_t tx_completed;
    uint32_t tx_errors;
};

/* 启动期一次注册全部后端、逻辑端口与路由。配置/ops/context 持续有效至重启。
 * 验证失败不发布配置，允许修正后重试；运行期不支持增删或替换注册项。
 * 不调用硬件初始化：先准备后端，注册成功后再开启接收。
 */
int vport_validate(const struct vport_config *config);
int vport_init(const struct vport_config *config);

/* 后端接收入口，线程/ISR 均可调用；复制元数据和有效载荷，永不等待。
 * 同一 link 的调用必须由后端按接收顺序串行提交。
 * 无匹配路由返回 -ENOENT，缓冲满返回 -EAGAIN，超长返回 -EMSGSIZE。
 * 流协议发现 sequence 跳变时，必须丢弃该路由已有半包并重新同步。
 */
int vport_rx_submit(uint16_t link, uint64_t peer, const struct vport_frame *frame);
/* 后端在 UART 溢出、丢包等数据缺口后调用，线程/ISR 均可用。
 * 递增该 link/peer 匹配的所有 RX 路由序号；下一份数据可观察到缺口。
 * 与同一 link 的 rx_submit 串行调用，不立即产生接收回调。
 */
int vport_rx_gap(uint16_t link, uint64_t peer);

/* 线程接口。发送必须显式选择已绑定的 link，不隐式广播。
 * CAN 的发送 ID 由 frame 指定，RX 过滤条件不会改写发送 ID。
 */
int vport_send(uint16_t port, uint16_t link, uint64_t peer,
               const struct vport_frame *frame, k_timeout_t timeout);
/* 固定回复来源 link/peer；协议仍须提供响应载荷及正确的 CAN ID。 */
int vport_reply(const struct vport_source *source, const struct vport_frame *frame,
                k_timeout_t timeout);

/* 协议事务可跨“发送—等待响应”持有物理链路；同线程 send 可重入。
 * 只协调本机调用者，不实现 RS485 多主仲裁；领取和释放必须在同一线程。
 * peer 也须匹配一条允许发送的路由。RX 投递不受此锁阻塞。
 */
int vport_claim(uint16_t port, uint16_t link, uint64_t peer, k_timeout_t timeout);
int vport_release(uint16_t link);
int vport_get_stats(uint16_t link, struct vport_stats *stats);

#endif
