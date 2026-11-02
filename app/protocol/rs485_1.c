#include "rs485_1.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

#define RS485_NODE DT_ALIAS(idu_forward)
#if DT_NODE_HAS_PROP(RS485_NODE, uart)
#define RS485_UART DT_PHANDLE(RS485_NODE, uart)
#else
#define RS485_UART DT_PARENT(RS485_NODE)
#endif

static const struct device *const uart = DEVICE_DT_GET(RS485_UART);
static const struct gpio_dt_spec direction = GPIO_DT_SPEC_GET(RS485_NODE, de_gpios);

/* 中断只缓存字节和时间戳，不解析报文。 */
typedef struct {
    uint64_t at_us;
    uint8_t byte;
} received_byte_t;

K_MSGQ_DEFINE(received, sizeof(received_byte_t), 256, 8);
static K_SEM_DEFINE(tx_done, 0, 1);
static atomic_t rx_lost;
static const uint8_t *tx_data;
static size_t tx_remaining;
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

static void uart_interrupt(const struct device *device, void *context)
{
    ARG_UNUSED(context);
    uart_irq_update(device);
    if (!tx_data && uart_err_check(device)) {
        atomic_set(&rx_lost, 1);
    }
    if (!tx_data && uart_irq_rx_ready(device)) {
        received_byte_t byte;
        while (uart_fifo_read(device, &byte.byte, 1) == 1) {
            byte.at_us = clock_us();
            if (k_msgq_put(&received, &byte, K_NO_WAIT)) {
                atomic_set(&rx_lost, 1);
            }
        }
    }
    if (uart_irq_tx_ready(device)) {
        if (tx_remaining) {
            int written = uart_fifo_fill(device, tx_data, tx_remaining);
            if (written > 0) {
                tx_data += written;
                tx_remaining -= written;
            }
        } else if (uart_irq_tx_complete(device)) {
            uart_irq_tx_disable(device);
            k_sem_give(&tx_done);
        }
    }
}

static int send_bytes(void *context, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    ARG_UNUSED(context);
    int err = gpio_pin_set_dt(&direction, 1);
    if (err) {
        return err;
    }
    uart_irq_rx_disable(uart);
    k_sem_reset(&tx_done);
    unsigned int key = irq_lock();
    tx_data = data;
    tx_remaining = size;
    uart_irq_tx_enable(uart);
    irq_unlock(key);
    err = k_sem_take(&tx_done, timeout);
    key = irq_lock();
    uart_irq_tx_disable(uart);
    tx_data = NULL;
    tx_remaining = 0;
    irq_unlock(key);
    int gpio_error = gpio_pin_set_dt(&direction, 0);
    uint8_t discard;
    while (uart_fifo_read(uart, &discard, 1) == 1) {
    }
    uart_irq_rx_enable(uart);
    return err ? -ETIMEDOUT : gpio_error;
}

int rs485_1_init(vport_t *port)
{
    if (!port) {
        return -EINVAL;
    }
    if (!device_is_ready(uart) || !gpio_is_ready_dt(&direction)) {
        return -ENODEV;
    }
    uart_irq_rx_disable(uart);
    uart_irq_tx_disable(uart);
    int err = gpio_pin_configure_dt(&direction, GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }
    err = uart_irq_callback_user_data_set(uart, uart_interrupt, NULL);
    if (err) {
        return err;
    }
    port->send = send_bytes;
    port->send_context = NULL;
    clock_us();
    return 0;
}

void rs485_1_start(void)
{
    uart_irq_rx_enable(uart);
}

int rs485_1_receive(vport_event_t *event)
{
    /* 单接收线程借用此字节，下一次 receive 前完成同步消费。 */
    static received_byte_t byte;
    if (!event) {
        return -EINVAL;
    }
    if (atomic_cas(&rx_lost, 1, 0)) {
        unsigned int key = irq_lock();
        k_msgq_purge(&received);
        *event = (vport_event_t){.type = VPORT_ERROR, .at_us = clock_us()};
        irq_unlock(key);
        return 0;
    }
    if (!k_msgq_get(&received, &byte, K_MSEC(1))) {
        *event = (vport_event_t){
            .type = VPORT_DATA,
            .data = &byte.byte,
            .size = 1,
            .at_us = byte.at_us,
        };
        return 0;
    }
    /* 队列非空时不能交付较新的 POLL，下一轮先读取已接收字节。 */
    unsigned int key = irq_lock();
    bool empty = !k_msgq_num_used_get(&received);
    *event = (vport_event_t){.type = VPORT_POLL, .at_us = clock_us()};
    irq_unlock(key);
    return empty ? 0 : -EAGAIN;
}
