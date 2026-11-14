#include <rs485/rs485.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/clock.h>
#include <errno.h>
#include <limits.h>

/* 所有辅助函数均由实例锁保护；清理最多读取 256 字节，禁止持续输入导致死循环。 */
#define RX_DRAIN_LIMIT 256

static void update_rx(rs485_t *instance)
{
    if (instance->config->rx_ready && instance->rx_enabled && !instance->rx_blocked &&
        !instance->fault && !instance->tx_data) {
        uart_irq_rx_enable(instance->config->uart);
    } else {
        uart_irq_rx_disable(instance->config->uart);
    }
}

static int drain_rx(rs485_t *instance)
{
    uint8_t discard;
    for (size_t i = 0; i < RX_DRAIN_LIMIT; ++i) {
        int err = uart_poll_in(instance->config->uart, &discard);
        if (err == -1) {
            return 0;
        }
        if (err < 0) {
            return err;
        }
    }
    return -EAGAIN;
}

static void finish_write(rs485_t *instance, int result)
{
    uart_irq_tx_disable(instance->config->uart);
    instance->tx_data = NULL;
    instance->tx_remaining = 0;
    /* 软件 DE 尚有效时清理回显，避免释放总线后丢弃对端的快速响应。 */
    int cleanup = drain_rx(instance);
    int err = instance->config->de_mode == RS485_DE_SOFTWARE
                  ? gpio_pin_set_dt(&instance->config->direction, 0)
                  : 0;
    result = result ? result : err;
    /* 先记录原始发送错误；清理错误不能覆盖它。通用 UART 接口不能保证硬件中止。 */
    if (result && !instance->fault) {
        instance->fault = result;
    }
    if (cleanup) {
        instance->rx_blocked = true;
        result = result ? result : cleanup;
    }
    instance->tx_result = result;
    update_rx(instance);
}

static void uart_interrupt(const struct device *device, void *context)
{
    rs485_t *instance = context;
    uart_irq_update(device);
    k_spinlock_key_t key = k_spin_lock(&instance->tx_lock);
    if (instance->tx_data) {
        int err = 0;
        if (instance->tx_remaining) {
            int ready = uart_irq_tx_ready(device);
            if (ready < 0) {
                err = ready;
            } else if (ready > 0) {
                int written = uart_fifo_fill(
                    device, instance->tx_data, MIN(instance->tx_remaining, (size_t)INT_MAX));
                if (written <= 0) {
                    err = written < 0 ? written : -EIO;
                } else if ((size_t)written > instance->tx_remaining) {
                    err = -EIO;
                } else {
                    instance->tx_data += written;
                    instance->tx_remaining -= written;
                }
            }
        }
        bool complete = false;
        if (!err && !instance->tx_remaining) {
            int status = uart_irq_tx_complete(device);
            err = status < 0 ? status : 0;
            complete = status > 0;
        }
        if (err || complete) {
            finish_write(instance, err);
            k_sem_give(&instance->tx_done);
        }
        k_spin_unlock(&instance->tx_lock, key);
        return;
    }
    bool notify = false;
    if (instance->config->rx_ready && instance->rx_enabled && !instance->rx_blocked &&
        !instance->fault) {
        int error = uart_err_check(device);
        int ready = uart_irq_rx_ready(device);
        if (error || ready < 0) {
            instance->rx_error = error < 0 ? error : (error ? -EIO : ready);
            instance->rx_blocked = true;
            uart_irq_rx_disable(device);
            (void)drain_rx(instance);
            notify = true;
        } else {
            notify = ready > 0;
        }
    }
    k_spin_unlock(&instance->tx_lock, key);
    if (notify) {
        instance->config->rx_ready(instance->config->rx_context);
    }
}

int rs485_init(rs485_t *instance, const rs485_config_t *config)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!instance || !config || !config->uart ||
        (config->de_mode != RS485_DE_SOFTWARE && config->de_mode != RS485_DE_HARDWARE)) {
        return -EINVAL;
    }
    if (instance->initialized) {
        return -EALREADY;
    }
    if (!device_is_ready(config->uart)) {
        return -ENODEV;
    }
    const struct uart_driver_api *api = DEVICE_API_GET(uart, config->uart);
    if (!api->fifo_fill || !api->poll_in || !api->err_check || !api->irq_tx_ready ||
        !api->irq_tx_complete || !api->irq_tx_enable || !api->irq_tx_disable ||
        !api->irq_rx_enable || !api->irq_rx_disable || !api->irq_callback_set) {
        return -ENOTSUP;
    }
    if (config->rx_ready && (!api->fifo_read || !api->irq_rx_ready)) {
        return -ENOTSUP;
    }
    if (config->de_mode == RS485_DE_SOFTWARE &&
        (!config->direction.port || !gpio_is_ready_dt(&config->direction))) {
        return -ENODEV;
    }
    struct uart_config uart_config;
    int err = uart_config_get(config->uart, &uart_config);
    if (err) {
        return err;
    }
    uart_config.flow_ctrl =
        config->de_mode == RS485_DE_HARDWARE ? UART_CFG_FLOW_CTRL_RS485 : UART_CFG_FLOW_CTRL_NONE;
    err = uart_configure(config->uart, &uart_config);
    if (err) {
        return err;
    }
    uart_irq_rx_disable(config->uart);
    uart_irq_tx_disable(config->uart);
    if (config->de_mode == RS485_DE_SOFTWARE) {
        err = gpio_pin_configure_dt(&config->direction, GPIO_OUTPUT_INACTIVE);
        if (err) {
            return err;
        }
    }
    instance->config = config;
    k_sem_init(&instance->tx_done, 0, 1);
    err = uart_irq_callback_user_data_set(config->uart, uart_interrupt, instance);
    if (err) {
        return err;
    }
    instance->initialized = true;
    return 0;
}

int rs485_rx_enable(rs485_t *instance, bool enabled)
{
    if (!instance || !instance->initialized) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&instance->tx_lock);
    int err = enabled ? instance->fault : 0;
    if (!err) {
        instance->rx_enabled = enabled;
        update_rx(instance);
    }
    k_spin_unlock(&instance->tx_lock, key);
    return err;
}

int rs485_flush_rx(rs485_t *instance)
{
    if (!instance || !instance->initialized) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&instance->tx_lock);
    if (instance->tx_busy) {
        k_spin_unlock(&instance->tx_lock, key);
        return -EBUSY;
    }
    uart_irq_rx_disable(instance->config->uart);
    int error = uart_err_check(instance->config->uart);
    int err = error < 0 ? error : drain_rx(instance);
    instance->rx_blocked = err != 0;
    if (!err) {
        instance->rx_error = 0;
    }
    update_rx(instance);
    k_spin_unlock(&instance->tx_lock, key);
    return err;
}

static int read_available(rs485_t *instance, uint8_t *data, size_t capacity)
{
    k_spinlock_key_t key = k_spin_lock(&instance->tx_lock);
    int result = 0;
    if (instance->fault) {
        result = instance->fault;
    } else if (instance->tx_data) {
        result = -EBUSY;
    } else if (instance->rx_error) {
        result = instance->rx_error;
        instance->rx_error = 0;
    } else if (!instance->rx_enabled || instance->rx_blocked) {
        result = -EACCES;
    } else {
        int error = uart_err_check(instance->config->uart);
        if (error) {
            result = error < 0 ? error : -EIO;
            instance->rx_blocked = true;
            uart_irq_rx_disable(instance->config->uart);
            (void)drain_rx(instance);
        } else if (instance->config->rx_ready) {
            int ready = uart_irq_rx_ready(instance->config->uart);
            result = ready > 0 ? uart_fifo_read(instance->config->uart, data, capacity) : ready;
            if (result < 0) {
                instance->rx_blocked = true;
                uart_irq_rx_disable(instance->config->uart);
                (void)drain_rx(instance);
            }
        } else {
            while ((size_t)result < capacity) {
                int err = uart_poll_in(instance->config->uart, &data[result]);
                if (err == -1) {
                    break;
                }
                if (err < 0) {
                    result = result ? result : err;
                    break;
                }
                ++result;
            }
        }
    }
    k_spin_unlock(&instance->tx_lock, key);
    return result;
}

int rs485_read(rs485_t *instance, uint8_t *data, size_t capacity, k_timeout_t timeout)
{
    if (!instance || !instance->initialized || !data || !capacity || capacity > INT_MAX) {
        return -EINVAL;
    }
    bool no_wait = K_TIMEOUT_EQ(timeout, K_NO_WAIT);
    if ((instance->config->rx_ready || k_is_in_isr()) && !no_wait) {
        return -EWOULDBLOCK;
    }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    while (true) {
        int result = read_available(instance, data, capacity);
        if (result || no_wait) {
            return result;
        }
        if (sys_timepoint_expired(deadline)) {
            return -ETIMEDOUT;
        }
        k_sleep(K_TICKS(1));
    }
}

int rs485_write(rs485_t *instance, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!instance || !instance->initialized || !data || !size) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&instance->tx_lock);
    int err = instance->tx_busy ? -EBUSY : instance->fault;
    if (!err && instance->rx_blocked) {
        err = -EAGAIN;
    }
    if (err) {
        k_spin_unlock(&instance->tx_lock, key);
        return err;
    }
    instance->tx_busy = true;
    uart_irq_rx_disable(instance->config->uart);
    err = instance->config->de_mode == RS485_DE_SOFTWARE
              ? gpio_pin_set_dt(&instance->config->direction, 1)
              : 0;
    if (err) {
        instance->fault = err;
        instance->tx_busy = false;
        k_spin_unlock(&instance->tx_lock, key);
        return err;
    }
    k_sem_reset(&instance->tx_done);
    instance->tx_data = data;
    instance->tx_remaining = size;
    instance->tx_result = -EINPROGRESS;
    uart_irq_tx_enable(instance->config->uart);
    k_spin_unlock(&instance->tx_lock, key);
    (void)k_sem_take(&instance->tx_done, timeout);
    key = k_spin_lock(&instance->tx_lock);
    if (instance->tx_data) {
        finish_write(instance, -ETIMEDOUT);
    }
    err = instance->tx_result;
    instance->tx_busy = false;
    k_spin_unlock(&instance->tx_lock, key);
    return err;
}
