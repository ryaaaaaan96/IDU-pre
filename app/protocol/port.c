#include "port.h"
#include "process_table.h"
#include "signal_keys.h"
#include "status/inc/status.h"
#include <transmit/transmit.h>
#include <dataprocess/dataprocess.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_port, LOG_LEVEL_INF);

#define FORWARD_NODE DT_ALIAS(idu_forward)
#if DT_NODE_HAS_PROP(FORWARD_NODE, uart)
#define FORWARD_UART DT_PHANDLE(FORWARD_NODE, uart)
#else
#define FORWARD_UART DT_PARENT(FORWARD_NODE)
#endif
#define FORWARD_BAUD DT_PROP(FORWARD_UART, current_speed)
#define FORWARD_GAP_US (FORWARD_BAUD > 19200 ? 1750 : DIV_ROUND_UP(35000000U, FORWARD_BAUD))

static const struct device *const uart = DEVICE_DT_GET(FORWARD_UART);
static const struct gpio_dt_spec direction = GPIO_DT_SPEC_GET(FORWARD_NODE, de_gpios);
extern const tr_modbus_point_t protocol_forward_points[];
extern const size_t protocol_forward_point_count;

/* 中断只接收字节和时间戳；判帧、DP 访问及发送均由 APP 线程完成。 */
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
static K_THREAD_STACK_DEFINE(port_stack, 4096);
static struct k_thread port_thread;

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

static vport_t forward_port = {.send = send_bytes};
/* 启动时读取站号并填充，绑定后不再修改。 */
static tr_protocol_t protocols[] = {
    {.type = TR_PROTOCOL_MODBUS,
        .max_frame = 256,
        .timeout_us = 100000,
        .modbus = {.gap_us = FORWARD_GAP_US}},
};
static const tr_config_t ports[] = {
    {.port = &forward_port,
        .protocols = protocols,
        .protocol_count = ARRAY_SIZE(protocols),
        .rx_capacity = 256,
        .tx_capacity = 256,
        .send_timeout_ms = 100},
};

static void deliver(vport_event_t *event)
{
    if (status_phase_get() != STATUS_RUN) {
        return;
    }
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        int err = vport_input(ports[i].port, event);
        if (err && err != -EIO) {
            LOG_WRN("Port %u event failed (%d)", (unsigned)i, err);
        }
    }
}

static void run(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    while (true) {
        if (atomic_cas(&rx_lost, 1, 0)) {
            unsigned int key = irq_lock();
            k_msgq_purge(&received);
            vport_event_t error = {.type = VPORT_ERROR, .at_us = clock_us()};
            irq_unlock(key);
            deliver(&error);
        }
        received_byte_t byte;
        if (!k_msgq_get(&received, &byte, K_MSEC(1))) {
            vport_event_t data = {
                .type = VPORT_DATA, .data = &byte.byte, .size = 1, .at_us = byte.at_us};
            deliver(&data);
        } else {
            /* 先检查队列，确保超时推进不会越过已接收但未交付的字节。 */
            unsigned int key = irq_lock();
            bool empty = !k_msgq_num_used_get(&received);
            vport_event_t poll = {.type = VPORT_POLL, .at_us = clock_us()};
            irq_unlock(key);
            if (empty) {
                deliver(&poll);
            }
        }
    }
}

int protocol_port_init(void)
{
    if (!device_is_ready(uart) || !gpio_is_ready_dt(&direction)) {
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(&direction, GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }
    err = uart_irq_callback_user_data_set(uart, uart_interrupt, NULL);
    if (err) {
        return err;
    }
    value_t address = {0};
    err = dp_get_para_by_index(DEVICE_LOCAL, PROCESS_SAVED_ADDRESS, VALUE_TYPE_U8, &address);
    if (err) {
        return err;
    }
    value_t current = {.u16 = address.u8};
    err = dp_set_para_by_index(DEVICE_LOCAL, PROCESS_ADDRESS, VALUE_TYPE_U16, &current);
    if (err) {
        return err;
    }
    protocols[0].modbus.unit = address.u8;
    protocols[0].modbus.points = protocol_forward_points;
    protocols[0].modbus.point_count = protocol_forward_point_count;
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        err = tr_register(&ports[i]);
        if (err) {
            return err;
        }
    }
    clock_us();
    k_thread_create(&port_thread,
        port_stack,
        K_THREAD_STACK_SIZEOF(port_stack),
        run,
        NULL,
        NULL,
        NULL,
        8,
        0,
        K_NO_WAIT);
    k_thread_name_set(&port_thread, "protocol_port");
    uart_irq_rx_enable(uart);
    LOG_INF("Modbus unit %u ready on forwarding UART", address.u8);
    return 0;
}
