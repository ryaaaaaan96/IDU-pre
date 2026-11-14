/* 平台异步扩展；调用及 ISR 通过 irq_lock 串行，回调允许补充接收缓冲区。 */
static void emit(const struct device *dev, struct uart_event *event)
{
    struct gd32_usart_data *d = dev->data;
    if (d->callback) {
        d->callback(dev, event, d->context);
    }
}

#include "usart_gd32_rx.inc"

static void end_tx(const struct device *dev, bool aborted)
{
    struct gd32_usart_data *d = dev->data;
    const struct gd32_usart_config *c = dev->config;
    if (!d->tx || d->tx_finishing) {
        return;
    }
    d->tx_finishing = true;
    struct dma_status status = {0};
    size_t sent = d->tx_size;
    usart_dma_transmit_config(c->reg, USART_DENT_DISABLE);
    if (aborted && !dma_get_status(c->dma, c->channel, &status)) {
        sent -= MIN(sent, status.pending_length);
    }
    (void)dma_stop(c->dma, c->channel);
    usart_gd32_irq_tx_disable(dev);
    (void)k_work_cancel_delayable(&d->tx_timeout);
    struct uart_event e = {
        .type = aborted ? UART_TX_ABORTED : UART_TX_DONE, .data.tx = {.buf = d->tx, .len = sent}};
    d->tx = NULL;
    d->tx_finishing = false;
    emit(dev, &e);
}

static int async_abort(const struct device *dev)
{
    unsigned int key = irq_lock();
    struct gd32_usart_data *d = dev->data;
    int err = d->tx ? 0 : -EFAULT;
    if (!err) {
        end_tx(dev, true);
    }
    irq_unlock(key);
    return err;
}

static void tx_timeout(struct k_work *work)
{
    struct gd32_usart_data *d =
        CONTAINER_OF(k_work_delayable_from_work(work), struct gd32_usart_data, tx_timeout);
    unsigned int key = irq_lock();
    if (d->tx && sys_timepoint_expired(d->deadline)) {
        (void)async_abort(d->dev);
    }
    irq_unlock(key);
}

static void dma_done(const struct device *dma, void *context, uint32_t channel, int status)
{
    ARG_UNUSED(dma);
    ARG_UNUSED(channel);
    const struct device *dev = context;
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    unsigned int key = irq_lock();
    if (d->tx) {
        if (status < 0) {
            end_tx(dev, true);
        } else {
            usart_dma_transmit_config(c->reg, USART_DENT_DISABLE);
            usart_gd32_irq_tx_enable(dev);
        }
    }
    irq_unlock(key);
}

static int async_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    if (!c->dma) {
        return -ENOTSUP;
    }
    if (!buf || !len || len > UINT16_MAX || !d->callback) {
        return -EINVAL;
    }
    if (timeout < 0 && timeout != SYS_FOREVER_US) {
        return -EINVAL;
    }
    unsigned int key = irq_lock();
    if (d->tx) {
        irq_unlock(key);
        return -EBUSY;
    }
    struct dma_block_config block = {
        .source_address = (uint32_t)(uintptr_t)buf,
        .dest_address = gd32_usart_tx_address(c->reg),
        .block_size = len,
        .source_addr_adj = DMA_ADDR_ADJ_INCREMENT,
        .dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
    };
    struct dma_config cfg = {.channel_direction = MEMORY_TO_PERIPHERAL,
        .source_data_size = 1,
        .dest_data_size = 1,
        .source_burst_length = 1,
        .dest_burst_length = 1,
        .channel_priority = c->priority,
        .block_count = 1,
        .head_block = &block,
        .dma_callback = dma_done,
        .user_data = (void *)dev};
    int err = dma_config(c->dma, c->channel, &cfg);
    if (!err) {
        usart_gd32_irq_tx_disable(dev);
        gd32_usart_flag_clear(c->reg, TC);
        d->tx = buf;
        d->tx_size = len;
        d->deadline = sys_timepoint_calc(timeout == SYS_FOREVER_US ? K_FOREVER : K_USEC(timeout));
        err = dma_start(c->dma, c->channel);
        if (!err) {
            usart_dma_transmit_config(c->reg, USART_DENT_ENABLE);
            if (timeout != SYS_FOREVER_US) {
                k_work_reschedule(&d->tx_timeout, K_USEC(timeout));
            }
        } else {
            (void)dma_stop(c->dma, c->channel);
            d->tx = NULL;
        }
    }
    irq_unlock(key);
    return err;
}

static int async_callback_set(const struct device *dev, uart_callback_t callback, void *context)
{
    struct gd32_usart_data *d = dev->data;
    unsigned int key = irq_lock();
    if (d->tx || d->rx || d->rx_stopping) {
        irq_unlock(key);
        return -EBUSY;
    }
    if (callback && d->user_cb) {
        irq_unlock(key);
        return -EBUSY;
    }
    d->callback = callback;
    d->context = context;
    irq_unlock(key);
    return 0;
}

static void async_isr(const struct device *dev)
{
    struct gd32_usart_data *d = dev->data;
    const struct gd32_usart_config *c = dev->config;
    unsigned int key = irq_lock();
    async_rx_isr(dev);
    if (d->tx && gd32_usart_interrupt_flag_get(c->reg, TC)) {
        end_tx(dev, false);
    }
    irq_unlock(key);
}
