#include <rs485/rs485.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/clock.h>
#include <errno.h>
#include <limits.h>

/* 所有状态由实例锁保护；调用 UART 异步函数时不持锁，允许同步事件回调。 */
static void event_callback(const struct device *dev, struct uart_event *event, void *context)
{
    rs485_t *s = context;
    struct uart_event delivered = *event;
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    bool notify = true;
    if (event->type == UART_TX_DONE || event->type == UART_TX_ABORTED) {
        if (!s->tx_data) {
            k_spin_unlock(&s->tx_lock, key);
            return;
        }
        int err =
            s->config->de_mode == RS485_DE_SOFTWARE ? gpio_pin_set_dt(&s->config->direction, 0) : 0;
        s->tx_result = event->type == UART_TX_ABORTED ? -EIO : err;
        if (s->tx_result && !s->fault) {
            s->fault = s->tx_result;
        }
        if (s->tx_result) {
            delivered.type = UART_TX_ABORTED;
        }
        s->tx_data = NULL;
        if (s->synchronous_tx) {
            notify = false;
            k_sem_give(&s->tx_done);
        } else {
            s->tx_busy = false;
        }
    } else if (event->type == UART_RX_RDY) {
        notify = !s->tx_data && !s->fault && !s->rx_blocked;
    } else if (event->type == UART_RX_STOPPED) {
        s->rx_error = -EIO;
        s->rx_blocked = true;
    } else if (event->type == UART_RX_DISABLED) {
        s->receiving = false;
    }
    uart_callback_t callback = s->callback;
    void *callback_context = s->callback_context;
    k_spin_unlock(&s->tx_lock, key);
    if (notify && callback) {
        callback(dev, &delivered, callback_context);
    }
}

int rs485_init(rs485_t *s, const rs485_config_t *config)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!s || !config || !config->uart ||
        (config->de_mode != RS485_DE_SOFTWARE && config->de_mode != RS485_DE_HARDWARE)) {
        return -EINVAL;
    }
    if (config->rx_ready) {
        return -ENOTSUP;
    }
    if (s->initialized) {
        return -EALREADY;
    }
    if (!device_is_ready(config->uart)) {
        return -ENODEV;
    }
    const struct uart_driver_api *api = DEVICE_API_GET(uart, config->uart);
    if (!api->poll_in || !api->callback_set || !api->tx || !api->tx_abort || !api->rx_enable ||
        !api->rx_disable || !api->rx_buf_rsp) {
        return -ENOTSUP;
    }
    struct uart_config cfg;
    int err = uart_config_get(config->uart, &cfg);
    if (err) {
        return err;
    }
    cfg.flow_ctrl =
        config->de_mode == RS485_DE_HARDWARE ? UART_CFG_FLOW_CTRL_RS485 : UART_CFG_FLOW_CTRL_NONE;
    err = uart_configure(config->uart, &cfg);
    if (err) {
        return err;
    }
    if (config->de_mode == RS485_DE_SOFTWARE) {
        if (!gpio_is_ready_dt(&config->direction)) {
            return -ENODEV;
        }
        err = gpio_pin_configure_dt(&config->direction, GPIO_OUTPUT_INACTIVE);
        if (err) {
            return err;
        }
    }
    s->config = config;
    k_sem_init(&s->tx_done, 0, 1);
    err = uart_callback_set(config->uart, event_callback, s);
    if (!err) {
        s->initialized = true;
    }
    return err;
}

int rs485_callback_set(rs485_t *s, uart_callback_t callback, void *context)
{
    if (!s || !s->initialized) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    int err = s->tx_busy || s->receiving || s->rx_starting ? -EBUSY : 0;
    if (!err) {
        s->callback = callback;
        s->callback_context = context;
    }
    k_spin_unlock(&s->tx_lock, key);
    return err;
}

int rs485_rx_start(rs485_t *s, uint8_t *buffer, size_t size, int32_t timeout_us)
{
    if (!s || !s->initialized || !buffer || !size) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    int err = s->fault ? s->fault
                       : (!s->callback ? -EINVAL : (s->receiving || s->rx_starting ? -EBUSY : 0));
    if (!err) {
        s->receiving = true;
        s->rx_starting = true;
        s->rx_blocked = false;
        s->rx_error = 0;
    }
    k_spin_unlock(&s->tx_lock, key);
    if (err) {
        return err;
    }
    err = uart_rx_enable(s->config->uart, buffer, size, timeout_us);
    key = k_spin_lock(&s->tx_lock);
    s->rx_starting = false;
    if (err) {
        s->receiving = false;
    }
    k_spin_unlock(&s->tx_lock, key);
    return err;
}

int rs485_rx_buf_rsp(rs485_t *s, uint8_t *buffer, size_t size)
{
    if (!s || !s->initialized || !buffer || !size) {
        return -EINVAL;
    }
    return uart_rx_buf_rsp(s->config->uart, buffer, size);
}

int rs485_rx_stop(rs485_t *s)
{
    if (!s || !s->initialized) {
        return -EINVAL;
    }
    return uart_rx_disable(s->config->uart);
}

/* 兼容同步轮询用法，不启动异步接收，也不安装第二套 UART 中断回调。 */
int rs485_rx_enable(rs485_t *s, bool enabled)
{
    if (!s || !s->initialized) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    int err = s->receiving || s->rx_starting ? -EBUSY : (enabled ? s->fault : 0);
    if (!err) {
        s->rx_enabled = enabled;
    }
    k_spin_unlock(&s->tx_lock, key);
    return err;
}

int rs485_flush_rx(rs485_t *s)
{
    if (!s || !s->initialized) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    if (s->tx_busy || s->receiving || s->rx_starting) {
        k_spin_unlock(&s->tx_lock, key);
        return -EBUSY;
    }
    int err = 0;
    uint8_t discard;
    for (size_t i = 0; i < 256; ++i) {
        err = uart_poll_in(s->config->uart, &discard);
        if (err) {
            break;
        }
    }
    err = err == -1 ? 0 : (err ? err : -EAGAIN);
    if (!err) {
        s->rx_blocked = false;
        s->rx_error = 0;
    }
    k_spin_unlock(&s->tx_lock, key);
    return err;
}

int rs485_read(rs485_t *s, uint8_t *data, size_t capacity, k_timeout_t timeout)
{
    if (!s || !s->initialized || !data || !capacity || capacity > INT_MAX) {
        return -EINVAL;
    }
    bool no_wait = K_TIMEOUT_EQ(timeout, K_NO_WAIT);
    if (k_is_in_isr() && !no_wait) {
        return -EWOULDBLOCK;
    }
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    do {
        k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
        int result = s->receiving || s->rx_starting || s->tx_busy ? -EBUSY : s->fault;
        if (!result && (!s->rx_enabled || s->rx_blocked)) {
            result = -EACCES;
        }
        if (!result) {
            int error = uart_err_check(s->config->uart);
            if (error && error != -ENOSYS) {
                result = error < 0 ? error : -EIO;
                s->rx_blocked = true;
            }
        }
        while (!result || (result > 0 && (size_t)result < capacity)) {
            int err = uart_poll_in(s->config->uart, &data[result]);
            if (err) {
                if (!result && err != -1) {
                    result = err;
                }
                break;
            }
            result++;
        }
        k_spin_unlock(&s->tx_lock, key);
        if (result || no_wait) {
            return result;
        }
        if (sys_timepoint_expired(deadline)) {
            return -ETIMEDOUT;
        }
        k_sleep(K_TICKS(1));
    } while (true);
}

static int start_tx(rs485_t *s, const uint8_t *data, size_t size, int32_t timeout_us, bool sync)
{
    if (!s || !s->initialized || !data || !size) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&s->tx_lock);
    int err = s->tx_busy ? -EBUSY : (s->fault ? s->fault : (s->rx_blocked ? -EAGAIN : 0));
    if (!err && !sync && !s->callback) {
        err = -EINVAL;
    }
    if (err) {
        k_spin_unlock(&s->tx_lock, key);
        return err;
    }
    s->tx_busy = true;
    s->synchronous_tx = sync;
    s->tx_data = data;
    s->tx_result = -EINPROGRESS;
    k_sem_reset(&s->tx_done);
    err = s->config->de_mode == RS485_DE_SOFTWARE ? gpio_pin_set_dt(&s->config->direction, 1) : 0;
    k_spin_unlock(&s->tx_lock, key);
    if (!err) {
        err = uart_tx(s->config->uart, data, size, timeout_us);
    }
    if (err) {
        key = k_spin_lock(&s->tx_lock);
        int cleanup =
            s->config->de_mode == RS485_DE_SOFTWARE ? gpio_pin_set_dt(&s->config->direction, 0) : 0;
        if (cleanup) {
            s->fault = cleanup;
        }
        s->tx_data = NULL;
        s->tx_busy = false;
        k_spin_unlock(&s->tx_lock, key);
    }
    return err;
}

int rs485_tx(rs485_t *s, const uint8_t *data, size_t size, int32_t timeout_us)
{
    return start_tx(s, data, size, timeout_us, false);
}

int rs485_tx_abort(rs485_t *s)
{
    if (!s || !s->initialized) {
        return -EINVAL;
    }
    return uart_tx_abort(s->config->uart);
}

int rs485_write(rs485_t *s, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    int err = start_tx(s, data, size, SYS_FOREVER_US, true);
    if (err) {
        return err;
    }
    k_spinlock_key_t key;
    if (k_sem_take(&s->tx_done, timeout)) {
        /* 中止可同步回调，禁止持实例锁调用；等待终止事件后才释放用户缓冲区。 */
        (void)uart_tx_abort(s->config->uart);
        (void)k_sem_take(&s->tx_done, K_FOREVER);
        key = k_spin_lock(&s->tx_lock);
        if (s->tx_result) {
            s->tx_result = -ETIMEDOUT;
            s->fault = -ETIMEDOUT;
        }
        k_spin_unlock(&s->tx_lock, key);
    }
    key = k_spin_lock(&s->tx_lock);
    err = s->tx_result;
    s->tx_busy = false;
    k_spin_unlock(&s->tx_lock, key);
    return err;
}
