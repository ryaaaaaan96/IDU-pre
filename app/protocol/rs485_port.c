#include "rs485_port.h"
#include <rs485/rs485.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

static uint32_t last_cycle;
static uint64_t accumulated_cycles;

/* 频繁采样扩展 32 位硬件周期计数；小临界区使线程与 ISR 共用单调时基。 */
static uint64_t clock_us(void)
{
    unsigned int key = irq_lock();
    uint32_t current = k_cycle_get_32();
    accumulated_cycles += (uint32_t)(current - last_cycle);
    last_cycle = current;
    uint64_t result = k_cyc_to_us_floor64(accumulated_cycles);
    irq_unlock(key);
    return result;
}

/* 仅复制字节并更新计数，诊断不改变接收恢复流程。 */
static void queue_byte(rs485_port_t *port, const rs485_received_byte_t *byte)
{
    unsigned int key = irq_lock();
    atomic_inc(&port->rx_bytes);
    if (k_msgq_put(&port->received, byte, K_NO_WAIT)) {
        atomic_inc(&port->rx_dropped);
        atomic_set(&port->rx_lost, 1);
    } else {
        atomic_val_t used = k_msgq_num_used_get(&port->received);
        atomic_val_t peak = atomic_get(&port->queue_peak);
        while (used > peak && !atomic_cas(&port->queue_peak, peak, used)) {
            peak = atomic_get(&port->queue_peak);
        }
    }
    irq_unlock(key);
}

#ifdef CONFIG_RS485_ASYNC
static void receive_event(const struct device *uart, struct uart_event *event, void *context)
{
    ARG_UNUSED(uart);
    rs485_port_t *port = context;
    switch (event->type) {
    case UART_RX_BUF_REQUEST:
        for (size_t i = 0; i < ARRAY_SIZE(port->rx_buffers); ++i) {
            if (!atomic_test_and_set_bit(&port->rx_owned, i)) {
                int err = rs485_rx_buf_rsp(
                    &port->transport, port->rx_buffers[i], sizeof(port->rx_buffers[i]));
                if (err) {
                    atomic_clear_bit(&port->rx_owned, i);
                    atomic_set(&port->rx_lost, 1);
                }
                break;
            }
        }
        break;
    case UART_RX_BUF_RELEASED:
        for (size_t i = 0; i < ARRAY_SIZE(port->rx_buffers); ++i) {
            if (event->data.rx_buf.buf == port->rx_buffers[i]) {
                atomic_clear_bit(&port->rx_owned, i);
            }
        }
        break;
    case UART_RX_RDY: {
        unsigned int key = irq_lock();
        uint64_t end = clock_us();
        bool full = event->data.rx.offset + event->data.rx.len == RS485_RX_BUFFER_SIZE;
        /* 未满缓冲扣除配置的一字符空闲时间；硬件取整及 ISR 延迟仍有误差。 */
        if (!full) {
            end = end > port->rx_byte_us ? end - port->rx_byte_us : 0;
        }
        for (size_t i = 0; i < event->data.rx.len; ++i) {
            uint64_t trailing = (event->data.rx.len - i - 1) * port->rx_byte_us;
            uint64_t at = end > trailing ? end - trailing : 0;
            at = MAX(at, port->rx_watermark);
            rs485_received_byte_t byte = {
                .at_us = at,
                .byte = event->data.rx.buf[event->data.rx.offset + i],
            };
            port->rx_watermark = at;
            queue_byte(port, &byte);
        }
        irq_unlock(key);
        break;
    }
    case UART_RX_STOPPED:
        atomic_set(&port->rx_lost, 1);
        break;
    case UART_RX_DISABLED:
        atomic_clear(&port->rx_active);
        atomic_set(&port->rx_lost, 1);
        break;
    default:
        break;
    }
}

static int start_receive(rs485_port_t *port)
{
    if (atomic_get(&port->rx_active) || atomic_get(&port->rx_owned)) {
        return -EBUSY;
    }
    atomic_set(&port->rx_owned, BIT(0));
    atomic_set(&port->rx_active, 1);
    /* 显式使用一字符的微秒超时，不依赖目标驱动对 0 的处理。 */
    int err = rs485_rx_start(
        &port->transport, port->rx_buffers[0], sizeof(port->rx_buffers[0]), port->rx_byte_us);
    if (err) {
        atomic_clear(&port->rx_owned);
        atomic_clear(&port->rx_active);
    }
    return err;
}
#else
/* 模块只通知可读；字节缓存、时间戳和协议事件均由产品层管理。 */
static void receive_ready(void *context)
{
    rs485_port_t *port = context;
    rs485_received_byte_t byte;
    int count;
    while ((count = rs485_read(&port->transport, &byte.byte, 1, K_NO_WAIT)) > 0) {
        byte.at_us = clock_us();
        queue_byte(port, &byte);
    }
    if (count < 0) {
        atomic_set(&port->rx_lost, 1);
    }
}

#endif

static int send_bytes(void *context, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    rs485_port_t *port = context;
    if (!port->started || atomic_get(&port->offline)) {
        return -ENETDOWN;
    }
    int err = rs485_write(&port->transport, data, size, timeout);
    if (err) {
        atomic_inc(&port->tx_errors);
        atomic_set(&port->rx_lost, 1);
        /* 忙或提交参数错误不锁存；已提交发送的终止错误隔离到本通道。 */
        if (err != -EBUSY && err != -EINVAL && err != -EAGAIN) {
            atomic_set(&port->offline, 1);
        }
    } else {
        atomic_inc(&port->tx_frames);
        atomic_add(&port->tx_bytes, size);
    }
    return err;
}

static int read_event(void *context, vport_event_t *event)
{
    return rs485_port_receive(context, event);
}

int rs485_port_init(
    rs485_port_t *port, vport_t *vport, const rs485_config_t *config, vport_channel_id_t channel_id)
{
    if (!port || !vport || !config || !config->uart || !channel_id || port->transport.initialized) {
        return -EINVAL;
    }
    if (vport->state != VPORT_INIT) {
        return -EBUSY;
    }
    if (!device_is_ready(config->uart)) {
        return -ENODEV;
    }
    struct uart_config uart_config;
    int err = uart_config_get(config->uart, &uart_config);
    if (err) {
        return err;
    }
    if (!uart_config.baudrate || uart_config.data_bits != UART_CFG_DATA_BITS_8 ||
        uart_config.stop_bits != UART_CFG_STOP_BITS_1 ||
        uart_config.parity != UART_CFG_PARITY_NONE) {
        return -ENOTSUP;
    }
    port->baudrate = uart_config.baudrate;
    port->rx_byte_us = DIV_ROUND_UP(10000000U, uart_config.baudrate);
    port->config = *config;
#ifndef CONFIG_RS485_ASYNC
    port->config.rx_ready = receive_ready;
    port->config.rx_context = port;
#endif
    k_msgq_init(
        &port->received, port->queue_buffer, sizeof(rs485_received_byte_t), RS485_RX_QUEUE_SIZE);
    err = rs485_init(&port->transport, &port->config);
    if (err) {
        return err;
    }
#ifdef CONFIG_RS485_ASYNC
    err = rs485_callback_set(&port->transport, receive_event, port);
    if (err) {
        return err;
    }
#endif
    port->channel.channel_id = channel_id;
    port->channel.send = send_bytes;
    port->channel.send_context = port;
    port->channel.source = read_event;
    port->channel.source_context = port;
    clock_us();
    /* 硬件准备完成后发布完整 source/send，不引入业务状态。 */
    err = vport_channel_add(vport, &port->channel);
    if (err) {
        (void)rs485_port_stop(port);
    }
    return err;
}

int rs485_port_start(rs485_port_t *port)
{
    if (!port || !port->transport.initialized || port->started) {
        return -EINVAL;
    }
    if (atomic_get(&port->offline)) {
        return -ENETDOWN;
    }
#ifdef CONFIG_RS485_ASYNC
    int err = start_receive(port);
#else
    int err = rs485_rx_enable(&port->transport, true);
#endif
    if (!err) {
        port->started = true;
    }
    return err;
}

int rs485_port_stop(rs485_port_t *port)
{
    if (!port || !port->transport.initialized) {
        return -EINVAL;
    }
    port->started = false;
    atomic_set(&port->offline, 1);
#ifdef CONFIG_RS485_ASYNC
    return atomic_get(&port->rx_active) ? rs485_rx_stop(&port->transport) : 0;
#else
    return rs485_rx_enable(&port->transport, false);
#endif
}

int rs485_port_receive(rs485_port_t *port, vport_event_t *event)
{
    /* 单接收线程借用此字节，下一次 receive 前完成同步消费。 */
    if (!port || !event) {
        return -EINVAL;
    }
    if (!port->started || atomic_get(&port->offline)) {
        if (port->started) {
            (void)rs485_port_stop(port);
        }
        return -ENETDOWN;
    }
    if (atomic_get(&port->rx_lost)) {
        /* 恢复重试须留出异步停止和缓冲释放的调度窗口。 */
        k_msleep(1);
    }
    if (atomic_cas(&port->rx_lost, 1, 0)) {
        atomic_inc(&port->rx_errors);
#ifdef CONFIG_RS485_ASYNC
        if (atomic_get(&port->rx_active)) {
            (void)rs485_rx_stop(&port->transport);
        }
        if (atomic_get(&port->rx_active) || atomic_get(&port->rx_owned)) {
            atomic_set(&port->rx_lost, 1);
        } else {
            unsigned int key = irq_lock();
            k_msgq_purge(&port->received);
            atomic_clear(&port->rx_lost);
            irq_unlock(key);
            int err = start_receive(port);
            if (err) {
                atomic_set(&port->rx_lost, 1);
                if (err != -EBUSY && err != -EAGAIN) {
                    atomic_set(&port->offline, 1);
                }
            } else {
                atomic_inc(&port->recoveries);
            }
        }
        unsigned int key = irq_lock();
        port->rx_watermark = clock_us();
        *event = (vport_event_t){.type = VPORT_ERROR,
            .channel_id = port->channel.channel_id,
            .at_us = port->rx_watermark};
        irq_unlock(key);
#else
        unsigned int key = irq_lock();
        k_msgq_purge(&port->received);
        int err = rs485_flush_rx(&port->transport);
        if (err) {
            atomic_set(&port->rx_lost, 1);
            if (err != -EBUSY && err != -EAGAIN) {
                atomic_set(&port->offline, 1);
            }
        } else {
            atomic_inc(&port->recoveries);
        }
        *event = (vport_event_t){
            .type = VPORT_ERROR, .channel_id = port->channel.channel_id, .at_us = clock_us()};
        irq_unlock(key);
#endif
        return 0;
    }
    if (!k_msgq_get(&port->received, &port->byte, K_MSEC(1))) {
        *event = (vport_event_t){
            .type = VPORT_DATA,
            .channel_id = port->channel.channel_id,
            .data = &port->byte.byte,
            .size = 1,
            .at_us = port->byte.at_us,
        };
        return 0;
    }
    /* 队列非空时不能交付较新的 POLL，下一轮先读取已接收字节。 */
    unsigned int key = irq_lock();
    bool empty = !k_msgq_num_used_get(&port->received);
    uint64_t at = clock_us();
#ifdef CONFIG_RS485_ASYNC
    /* 下一批可能仍在 DMA 中。最多按每字节一帧传输加不足一帧静默预算，
     * 用落后水位推进 POLL，避免其越过尚未通知的接收字节。 */
    uint64_t window = (2U * RS485_RX_BUFFER_SIZE + 3U) * port->rx_byte_us;
    if (at < window || at - window < port->rx_watermark || atomic_get(&port->rx_lost)) {
        empty = false;
    } else {
        at -= window;
        if (empty) {
            port->rx_watermark = at;
        }
    }
#endif
    if (empty) {
        *event = (vport_event_t){
            .type = VPORT_POLL, .channel_id = port->channel.channel_id, .at_us = at};
    }
    irq_unlock(key);
    return empty ? 0 : -EAGAIN;
}
