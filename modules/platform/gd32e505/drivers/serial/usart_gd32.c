/*
 * Copyright (c) 2021, ATL Electronics
 * Copyright (c) 2025 Aleksandr Senin <al@meshium.net>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_usart

#include <errno.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/clock.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>

#include <gd32_usart.h>

#include "usart_gd32_regs.h"

struct gd32_usart_config {
    uint32_t reg;
    const struct device *dma;
    uint32_t channel, priority;
    const struct device *rx_dma;
    uint32_t rx_channel, rx_priority;
    uint16_t clkid;
    struct reset_dt_spec reset;
    const struct pinctrl_dev_config *pcfg;
    uint32_t parity;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    uart_irq_config_func_t irq_config_func;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

struct gd32_usart_data {
    uint32_t baud_rate;
    uart_callback_t callback;
    void *context;
    const struct device *dev;
    const uint8_t *tx;
    size_t tx_size;
    bool tx_finishing;
    struct k_work_delayable tx_timeout;
    k_timepoint_t deadline;
    uint8_t *rx, *next;
    size_t rx_size, rx_offset, next_size;
    size_t rx_count, rx_dma_base;
    uint32_t rx_epoch;
    bool rx_dma_running, rx_stopping;
    int32_t rx_timeout;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    uart_irq_callback_user_data_t user_cb;
    void *user_data;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
    enum uart_config_parity parity;
    enum uart_config_stop_bits stop_bits;
    enum uart_config_data_bits data_bits;
    enum uart_config_flow_control flow_ctrl;
    bool initialized;
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
};

static void async_isr(const struct device *dev);
static void tx_timeout(struct k_work *work);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void usart_gd32_isr(const struct device *dev)
{
    struct gd32_usart_data *const data = dev->data;

    if (data->callback) {
        async_isr(dev);
        return;
    }
    if (data->user_cb) {
        data->user_cb(dev, data->user_data);
    }
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static int usart_gd32_init(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;
    struct gd32_usart_data *const data = dev->data;
    data->dev = dev;
    k_work_init_delayable(&data->tx_timeout, tx_timeout);
    if ((cfg->dma && !device_is_ready(cfg->dma)) ||
        (cfg->rx_dma && !device_is_ready(cfg->rx_dma))) {
        return -ENODEV;
    }
    if (cfg->dma && cfg->dma == cfg->rx_dma && cfg->channel == cfg->rx_channel) {
        return -EINVAL;
    }
    uint32_t word_length;
    uint32_t parity;
    int ret;

    ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
    if (ret < 0) {
        return ret;
    }

    /**
     * In order to keep the transfer data size to 8 bits(1 byte),
     * append word length to 9BIT if parity bit enabled.
     */
    switch (cfg->parity) {
    case UART_CFG_PARITY_NONE:
        parity = USART_PM_NONE;
        word_length = USART_WL_8BIT;
        break;
    case UART_CFG_PARITY_ODD:
        parity = USART_PM_ODD;
        word_length = USART_WL_9BIT;
        break;
    case UART_CFG_PARITY_EVEN:
        parity = USART_PM_EVEN;
        word_length = USART_WL_9BIT;
        break;
    default:
        return -ENOTSUP;
    }

    (void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid);

    (void)reset_line_toggle_dt(&cfg->reset);

    usart_baudrate_set(cfg->reg, data->baud_rate);
    usart_parity_config(cfg->reg, parity);
    usart_word_length_set(cfg->reg, word_length);
    /* Default to 1 stop bit */
    usart_stop_bit_set(cfg->reg, USART_STB_1BIT);
    usart_receive_config(cfg->reg, USART_RECEIVE_ENABLE);
    usart_transmit_config(cfg->reg, USART_TRANSMIT_ENABLE);
    usart_enable(cfg->reg);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    cfg->irq_config_func(dev);
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
    /* Initialize runtime configuration from Devicetree defaults */
    data->parity = cfg->parity;
    data->data_bits = UART_CFG_DATA_BITS_8;
    data->stop_bits = UART_CFG_STOP_BITS_1;
    data->flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
    data->initialized = true;
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
    return 0;
}

static int usart_gd32_poll_in(const struct device *dev, unsigned char *c)
{
    const struct gd32_usart_config *const cfg = dev->config;
    uint32_t status;

    status = gd32_usart_flag_get(cfg->reg, RBNE);

    if (!status) {
        return -1;
    }

    *c = usart_data_receive(cfg->reg);

    return 0;
}

static void usart_gd32_poll_out(const struct device *dev, unsigned char c)
{
    const struct gd32_usart_config *const cfg = dev->config;

    usart_data_transmit(cfg->reg, c);

    while (gd32_usart_flag_get(cfg->reg, TBE) == RESET) {
        ;
    }
}

static int usart_gd32_err_check(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;
    uint32_t status = gd32_usart_status(cfg->reg);
    int errors = 0;

    if (status & USART_STAT0_ORERR) {
        errors |= UART_ERROR_OVERRUN;
    }
    if (status & USART_STAT0_PERR) {
        errors |= UART_ERROR_PARITY;
    }
    if (status & USART_STAT0_FERR) {
        errors |= UART_ERROR_FRAMING;
    }
    if (status & USART_STAT0_NERR) {
        errors |= UART_ERROR_NOISE;
    }
    if (errors) {
        gd32_usart_clear_rx(cfg->reg);
    }

    return errors;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int usart_gd32_configure(const struct device *dev, const struct uart_config *cfg_new)
{
    const struct gd32_usart_config *const cfg = dev->config;
    struct gd32_usart_data *const data = dev->data;
    uint32_t parity_bits;
    uint32_t word_length;
    uint32_t stop_bits_hw;

    if (data->tx || data->rx) {
        return -EBUSY;
    }
    if (cfg_new == NULL) {
        return -EINVAL;
    }

    if (cfg_new->baudrate == 0U) {
        return -EINVAL;
    }

    if (cfg_new->flow_ctrl != UART_CFG_FLOW_CTRL_NONE) {
        return -ENOTSUP;
    }

    switch (cfg_new->parity) {
    case UART_CFG_PARITY_NONE:
        parity_bits = USART_PM_NONE;
        break;
    case UART_CFG_PARITY_ODD:
        parity_bits = USART_PM_ODD;
        break;
    case UART_CFG_PARITY_EVEN:
        parity_bits = USART_PM_EVEN;
        break;
    default:
        return -EINVAL;
    }

    switch (cfg_new->data_bits) {
    case UART_CFG_DATA_BITS_8:
    case UART_CFG_DATA_BITS_7:
        break;
    default:
        return -EINVAL;
    }

    if (cfg_new->data_bits == UART_CFG_DATA_BITS_7 && cfg_new->parity == UART_CFG_PARITY_NONE) {
        return -EINVAL;
    }

    /* Map word length depending on requested data bits and parity */
    if (cfg_new->parity == UART_CFG_PARITY_NONE) {
        /* 8N* uses 8-bit word length */
        word_length = USART_WL_8BIT;
    } else {
        /* With parity: 8 data bits -> 9-bit word length, 7 data bits -> 8-bit */
        word_length = (cfg_new->data_bits == UART_CFG_DATA_BITS_8) ? USART_WL_9BIT : USART_WL_8BIT;
    }

    switch (cfg_new->stop_bits) {
    case UART_CFG_STOP_BITS_1:
        stop_bits_hw = USART_STB_1BIT;
        break;
    case UART_CFG_STOP_BITS_2:
        stop_bits_hw = USART_STB_2BIT;
        break;
    default:
        return -EINVAL;
    }

    if (data->baud_rate == cfg_new->baudrate && data->parity == cfg_new->parity &&
        data->data_bits == cfg_new->data_bits && data->stop_bits == cfg_new->stop_bits &&
        data->flow_ctrl == cfg_new->flow_ctrl) {
        return 0;
    }

    unsigned int key = irq_lock();

    usart_disable(cfg->reg);

    usart_parity_config(cfg->reg, parity_bits);
    usart_word_length_set(cfg->reg, word_length);
    usart_stop_bit_set(cfg->reg, stop_bits_hw);
    usart_baudrate_set(cfg->reg, cfg_new->baudrate);

    usart_receive_config(cfg->reg, USART_RECEIVE_ENABLE);
    usart_transmit_config(cfg->reg, USART_TRANSMIT_ENABLE);
    usart_enable(cfg->reg);

    irq_unlock(key);

    data->baud_rate = cfg_new->baudrate;
    data->parity = cfg_new->parity;
    data->data_bits = cfg_new->data_bits;
    data->stop_bits = cfg_new->stop_bits;
    data->flow_ctrl = cfg_new->flow_ctrl;

    return 0;
}

static int usart_gd32_config_get(const struct device *dev, struct uart_config *cfg_out)
{
    struct gd32_usart_data *const data = dev->data;

    if (cfg_out == NULL) {
        return -EINVAL;
    }

    if (!data->initialized) {
        return -ENODEV;
    }

    cfg_out->baudrate = data->baud_rate;
    cfg_out->parity = data->parity;
    cfg_out->stop_bits = data->stop_bits;
    cfg_out->data_bits = data->data_bits;
    cfg_out->flow_ctrl = data->flow_ctrl;

    return 0;
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
int usart_gd32_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
    const struct gd32_usart_config *const cfg = dev->config;
    int num_tx = 0U;

    while ((len - num_tx > 0) && gd32_usart_flag_get(cfg->reg, TBE)) {
        usart_data_transmit(cfg->reg, tx_data[num_tx++]);
    }

    return num_tx;
}

int usart_gd32_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
    const struct gd32_usart_config *const cfg = dev->config;
    int num_rx = 0U;

    while ((size - num_rx > 0) && gd32_usart_flag_get(cfg->reg, RBNE)) {
        rx_data[num_rx++] = usart_data_receive(cfg->reg);
    }

    return num_rx;
}

void usart_gd32_irq_tx_enable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_enable(cfg->reg, TC);
}

void usart_gd32_irq_tx_disable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_disable(cfg->reg, TC);
}

int usart_gd32_irq_tx_ready(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    return gd32_usart_flag_get(cfg->reg, TBE) && gd32_usart_interrupt_flag_get(cfg->reg, TC);
}

int usart_gd32_irq_tx_complete(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    return gd32_usart_flag_get(cfg->reg, TC);
}

void usart_gd32_irq_rx_enable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_enable(cfg->reg, RBNE);
}

void usart_gd32_irq_rx_disable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_disable(cfg->reg, RBNE);
}

int usart_gd32_irq_rx_ready(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    return gd32_usart_flag_get(cfg->reg, RBNE);
}

void usart_gd32_irq_err_enable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_enable(cfg->reg, ERR);
    gd32_usart_interrupt_enable(cfg->reg, PERR);
}

void usart_gd32_irq_err_disable(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    gd32_usart_interrupt_disable(cfg->reg, ERR);
    gd32_usart_interrupt_disable(cfg->reg, PERR);
}

int usart_gd32_irq_is_pending(const struct device *dev)
{
    const struct gd32_usart_config *const cfg = dev->config;

    return (
        (gd32_usart_flag_get(cfg->reg, RBNE) && gd32_usart_interrupt_flag_get(cfg->reg, RBNE)) ||
        (gd32_usart_flag_get(cfg->reg, TC) && gd32_usart_interrupt_flag_get(cfg->reg, TC)));
}

void usart_gd32_irq_callback_set(
    const struct device *dev, uart_irq_callback_user_data_t cb, void *user_data)
{
    struct gd32_usart_data *const data = dev->data;

    data->user_cb = cb;
    data->user_data = user_data;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#include "usart_gd32_async.inc"

static DEVICE_API(uart, usart_gd32_driver_api) = {
    .callback_set = async_callback_set,
    .tx = async_tx,
    .tx_abort = async_abort,
    .rx_enable = async_rx_enable,
    .rx_disable = async_rx_disable,
    .rx_buf_rsp = async_rx_buf_rsp,
    .poll_in = usart_gd32_poll_in,
    .poll_out = usart_gd32_poll_out,
    .err_check = usart_gd32_err_check,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
    .configure = usart_gd32_configure,
    .config_get = usart_gd32_config_get,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    .fifo_fill = usart_gd32_fifo_fill,
    .fifo_read = usart_gd32_fifo_read,
    .irq_tx_enable = usart_gd32_irq_tx_enable,
    .irq_tx_disable = usart_gd32_irq_tx_disable,
    .irq_tx_ready = usart_gd32_irq_tx_ready,
    .irq_tx_complete = usart_gd32_irq_tx_complete,
    .irq_rx_enable = usart_gd32_irq_rx_enable,
    .irq_rx_disable = usart_gd32_irq_rx_disable,
    .irq_rx_ready = usart_gd32_irq_rx_ready,
    .irq_err_enable = usart_gd32_irq_err_enable,
    .irq_err_disable = usart_gd32_irq_err_disable,
    .irq_is_pending = usart_gd32_irq_is_pending,
    .irq_callback_set = usart_gd32_irq_callback_set,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define GD32_USART_IRQ_HANDLER(n)                                                                  \
    static void usart_gd32_config_func_##n(const struct device *dev)                               \
    {                                                                                              \
        IRQ_CONNECT(                                                                               \
            DT_INST_IRQN(n), DT_INST_IRQ(n, priority), usart_gd32_isr, DEVICE_DT_INST_GET(n), 0);  \
        irq_enable(DT_INST_IRQN(n));                                                               \
    }
#define GD32_USART_IRQ_HANDLER_FUNC_INIT(n) .irq_config_func = usart_gd32_config_func_##n
#else /* CONFIG_UART_INTERRUPT_DRIVEN */
#define GD32_USART_IRQ_HANDLER(n)
#define GD32_USART_IRQ_HANDLER_FUNC_INIT(n)
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#define GD32_USART_INIT(n)                                                                         \
    PINCTRL_DT_INST_DEFINE(n);                                                                     \
    GD32_USART_IRQ_HANDLER(n)                                                                      \
    static struct gd32_usart_data usart_gd32_data_##n = {                                          \
        .baud_rate = DT_INST_PROP(n, current_speed),                                               \
    };                                                                                             \
    static const struct gd32_usart_config usart_gd32_config_##n = {.reg = DT_INST_REG_ADDR(n),     \
        COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                                  \
            (.dma = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx)),                               \
                .channel = DT_INST_DMAS_CELL_BY_NAME(n, tx, channel),                              \
                .priority = (DT_INST_DMAS_CELL_BY_NAME(n, tx, config) >> 16) & 3, ),               \
            ()) COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                          \
            (.rx_dma = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, rx)),                            \
                .rx_channel = DT_INST_DMAS_CELL_BY_NAME(n, rx, channel),                           \
                .rx_priority = (DT_INST_DMAS_CELL_BY_NAME(n, rx, config) >> 16) & 3, ),            \
            ())                                                                                    \
            .clkid = DT_INST_CLOCKS_CELL(n, id),                                                   \
        .reset = RESET_DT_SPEC_INST_GET(n),                                                        \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                                 \
        .parity = DT_INST_ENUM_IDX(n, parity),                                                     \
        GD32_USART_IRQ_HANDLER_FUNC_INIT(n)};                                                      \
    DEVICE_DT_INST_DEFINE(n,                                                                       \
        usart_gd32_init,                                                                           \
        NULL,                                                                                      \
        &usart_gd32_data_##n,                                                                      \
        &usart_gd32_config_##n,                                                                    \
        COND_CODE_1(UTIL_OR(DT_INST_DMAS_HAS_NAME(n, tx), DT_INST_DMAS_HAS_NAME(n, rx)),           \
            (POST_KERNEL),                                                                         \
            (PRE_KERNEL_1)),                                                                       \
        CONFIG_SERIAL_INIT_PRIORITY,                                                               \
        &usart_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GD32_USART_INIT)
