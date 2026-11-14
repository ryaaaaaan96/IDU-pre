/** @file @brief 每通道独立的 RS485 产品缓存、时序及事件适配。 */
#ifndef RS485_PORT_H
#define RS485_PORT_H

#include <rs485/rs485.h>
#include <vport/vport.h>
#include <zephyr/sys/atomic.h>

#define RS485_RX_BUFFER_SIZE 16U
#define RS485_RX_QUEUE_SIZE 256U

typedef struct {
    uint64_t at_us;
    uint8_t byte;
} rs485_received_byte_t;

/** @brief 常驻且初始化前清零；每 UART 独占一个实例，每实例单接收线程。 */
typedef struct {
    rs485_t transport;
    rs485_config_t config;
    vport_channel_t channel;
    struct k_msgq received;
    char queue_buffer[RS485_RX_QUEUE_SIZE * sizeof(rs485_received_byte_t)] __aligned(8);
    rs485_received_byte_t byte;
    atomic_t rx_lost;
    atomic_t offline;
    atomic_t rx_errors;
    atomic_t tx_errors;
    atomic_t recoveries;
    atomic_t rx_bytes;   /**< 已通知的 RX 字节，含队列丢弃，不含被模块过滤的回显。 */
    atomic_t rx_dropped; /**< 产品接收队列放入失败的字节数。 */
    atomic_t tx_bytes;   /**< 成功完成的发送字节数。 */
    atomic_t tx_frames;  /**< 成功完成的同步发送次数。 */
    atomic_t queue_peak; /**< 接收队列观测到的最大占用项数。 */
    uint32_t baudrate;
    uint32_t rx_byte_us;
    uint64_t rx_watermark;
    bool started;
#ifdef CONFIG_RS485_ASYNC
    uint8_t rx_buffers[2][RS485_RX_BUFFER_SIZE] __aligned(4);
    atomic_t rx_owned;
    atomic_t rx_active;
#endif
} rs485_port_t;

/** @brief 复制硬件配置并初始化、挂载完整 source/send 通道，不开放接收；实例和配置引用须常驻。 */
int rs485_port_init(rs485_port_t *port,
    vport_t *vport,
    const rs485_config_t *config,
    vport_channel_id_t channel_id);
/** @brief 协议绑定且 vport READY 后启动；全部端口成功后再运行 poll 线程。 */
int rs485_port_start(rs485_port_t *port);
/** @brief 停止新业务并请求关闭接收，不支持再次 start；async 缓冲等释放后才能销毁。 */
int rs485_port_stop(rs485_port_t *port);
/**
 * @brief 单线程读取事件，最多等待 1 ms；竞争返回 -EAGAIN，离线返回 -ENETDOWN。
 * @note DATA 指针仅在下次调用前有效。RX 错误局部恢复；离线故障在读取时锁存并请求自停，须重启。
 *       -ENETDOWN 不保证 DMA 停止完成或缓冲已释放。
 *       rx_errors 统计恢复轮次，tx_errors 统计失败发送，recoveries 统计成功恢复。
 */
int rs485_port_receive(rs485_port_t *port, vport_event_t *event);

#endif
