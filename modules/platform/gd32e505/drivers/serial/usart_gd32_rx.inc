/* SPDX-License-Identifier: Apache-2.0 */
/* RX DMA 使用普通模式和调用方双缓冲；没有循环覆盖或私有接收字节环。 */
static void rx_dma_done(const struct device *dma, void *context, uint32_t channel, int status);

static int rx_errors(uint32_t stat)
{
    return ((stat & USART_STAT0_ORERR) ? UART_ERROR_OVERRUN : 0) |
           ((stat & USART_STAT0_PERR) ? UART_ERROR_PARITY : 0) |
           ((stat & USART_STAT0_FERR) ? UART_ERROR_FRAMING : 0) |
           ((stat & USART_STAT0_NERR) ? UART_ERROR_NOISE : 0);
}

static void rx_interrupts_off(const struct device *dev)
{
    const struct gd32_usart_config *c = dev->config;
    usart_gd32_irq_rx_disable(dev);
    usart_gd32_irq_err_disable(dev);
    gd32_usart_interrupt_disable(c->reg, IDLE);
    if (c->reg == USART0 || c->reg == USART1 || c->reg == USART2 || c->reg == USART5) {
        gd32_usart_interrupt_disable(c->reg, RT);
        usart_receiver_timeout_disable(c->reg);
    }
}

/* 必须先确认 DMA 已停，才可释放用户缓冲区。 */
static void release_rx(const struct device *dev)
{
    struct gd32_usart_data *d = dev->data;
    uint8_t *current = d->rx, *next = d->next;
    d->rx = NULL;
    d->next = NULL;
    d->rx_epoch++;
    if (current) {
        struct uart_event e = {.type = UART_RX_BUF_RELEASED, .data.rx_buf.buf = current};
        emit(dev, &e);
    }
    if (next) {
        struct uart_event e = {.type = UART_RX_BUF_RELEASED, .data.rx_buf.buf = next};
        emit(dev, &e);
    }
    d->rx_stopping = false;
    struct uart_event e = {.type = UART_RX_DISABLED};
    emit(dev, &e);
}

static void rx_report(const struct device *dev)
{
    struct gd32_usart_data *d = dev->data;
    if (d->rx_count > d->rx_offset) {
        struct uart_event e = {.type = UART_RX_RDY,
            .data.rx = {.buf = d->rx, .offset = d->rx_offset, .len = d->rx_count - d->rx_offset}};
        d->rx_offset = d->rx_count;
        emit(dev, &e);
    }
}

static int rx_dma_pause(const struct device *dev)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    usart_dma_receive_config(c->reg, USART_DENR_DISABLE);
    if (!d->rx_dma_running) {
        return 0;
    }
    int err = dma_stop(c->rx_dma, c->rx_channel);
    if (err) {
        return err;
    }
    struct dma_status status = {0};
    err = dma_get_status(c->rx_dma, c->rx_channel, &status);
    if (!err) {
        if (status.pending_length > d->rx_size - d->rx_dma_base) {
            return -EIO;
        }
        size_t count = d->rx_size - status.pending_length;
        if (count < d->rx_count) {
            return -EIO;
        }
        d->rx_count = count;
        d->rx_dma_running = false;
    }
    return err;
}

static int rx_dma_resume(const struct device *dev)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    if (!d->rx || d->rx_stopping || d->rx_count == d->rx_size) {
        return -EACCES;
    }
    struct dma_block_config block = {
        .source_address = gd32_usart_rx_address(c->reg),
        .dest_address = (uint32_t)(uintptr_t)(d->rx + d->rx_count),
        .block_size = d->rx_size - d->rx_count,
        .source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
        .dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
    };
    struct dma_config cfg = {.channel_direction = PERIPHERAL_TO_MEMORY,
        .source_data_size = 1,
        .dest_data_size = 1,
        .source_burst_length = 1,
        .dest_burst_length = 1,
        .channel_priority = c->rx_priority,
        .block_count = 1,
        .head_block = &block,
        .dma_callback = rx_dma_done,
        .user_data = (void *)dev};
    d->rx_dma_base = d->rx_count;
    int err = dma_config(c->rx_dma, c->rx_channel, &cfg);
    if (!err) {
        err = dma_start(c->rx_dma, c->rx_channel);
        if (!err) {
            d->rx_dma_running = true;
            usart_dma_receive_config(c->reg, USART_DENR_ENABLE);
        } else {
            (void)dma_stop(c->rx_dma, c->rx_channel);
        }
    }
    return err;
}

static void rx_failed(const struct device *dev, int reason)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    if (!d->rx || d->rx_stopping) {
        return;
    }
    d->rx_stopping = true;
    rx_interrupts_off(dev);
    int err = c->rx_dma ? rx_dma_pause(dev) : 0;
    /* 停止 DMA 后按各实例的寄存器布局清除接收错误。 */
    if (!d->rx_dma_running) {
        gd32_usart_clear_rx(c->reg);
    }
    struct uart_event e = {.type = UART_RX_STOPPED,
        .data.rx_stop = {.reason = reason,
            .data = {.buf = d->rx, .offset = d->rx_offset, .len = d->rx_count - d->rx_offset}}};
    emit(dev, &e);
    if (!err) {
        rx_report(dev);
        release_rx(dev);
    } else {
        /* 停止失败时不归还 DMA 仍可能使用的缓冲区，允许调用者重试 disable。 */
        d->rx_stopping = false;
    }
}

/* 当前缓冲填满；先报告、切换并启动后备缓冲，再归还旧缓冲。 */
static void rx_full(const struct device *dev, bool restart)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    uint8_t *old = d->rx;
    uint32_t epoch = d->rx_epoch;
    rx_report(dev);
    if (d->rx_epoch != epoch || d->rx != old || d->rx_stopping) {
        return;
    }
    if (!d->next) {
        d->rx_stopping = true;
        rx_interrupts_off(dev);
        release_rx(dev);
        return;
    }
    d->rx = d->next;
    d->rx_size = d->next_size;
    d->next = NULL;
    d->rx_count = d->rx_offset = 0;
    int err = c->rx_dma && restart ? rx_dma_resume(dev) : 0;
    struct uart_event e = {.type = UART_RX_BUF_RELEASED, .data.rx_buf.buf = old};
    emit(dev, &e);
    if (d->rx_epoch != epoch || !d->rx || d->rx_stopping) {
        return;
    }
    if (err) {
        rx_failed(dev, UART_ERROR_OVERRUN);
        return;
    }
    e.type = UART_RX_BUF_REQUEST;
    emit(dev, &e);
}

/* RTF 按实例布局清除，不读取 DATA；仅按接收事件交付，不依赖 TX 状态。 */
static void rx_dma_service(const struct device *dev)
{
    struct gd32_usart_data *d = dev->data;
    uint32_t epoch = d->rx_epoch;
    if (rx_dma_pause(dev)) {
        rx_failed(dev, UART_ERROR_OVERRUN);
        return;
    }
    rx_report(dev);
    if (d->rx_epoch != epoch || !d->rx || d->rx_stopping) {
        return;
    }
    if (d->rx_count == d->rx_size) {
        rx_full(dev, false);
    }
    if (d->rx_epoch != epoch || !d->rx || d->rx_stopping) {
        return;
    }
    if (rx_dma_resume(dev)) {
        rx_failed(dev, UART_ERROR_OVERRUN);
    }
}

static void rx_dma_done(const struct device *dma, void *context, uint32_t channel, int status)
{
    ARG_UNUSED(dma);
    ARG_UNUSED(channel);
    const struct device *dev = context;
    struct gd32_usart_data *d = dev->data;
    unsigned int key = irq_lock();
    if (d->rx && d->rx_dma_running && !d->rx_stopping) {
        if (status < 0) {
            rx_failed(dev, UART_ERROR_OVERRUN);
        } else if (rx_dma_pause(dev)) {
            rx_failed(dev, UART_ERROR_OVERRUN);
        } else if (d->rx_count == d->rx_size) {
            rx_full(dev, true);
        } else if (rx_dma_resume(dev)) {
            rx_failed(dev, UART_ERROR_OVERRUN);
        }
    }
    irq_unlock(key);
}

static int async_rx_disable(const struct device *dev)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    unsigned int key = irq_lock();
    if (!d->rx || d->rx_stopping) {
        int err = d->rx_stopping ? -EBUSY : -EFAULT;
        irq_unlock(key);
        return err;
    }
    d->rx_stopping = true;
    rx_interrupts_off(dev);
    int err = c->rx_dma ? rx_dma_pause(dev) : 0;
    if (!err) {
        bool residual = false;
        uint8_t byte = 0;
        if (c->rx_dma) {
            uint32_t stat = gd32_usart_status(c->reg);
            residual = (stat & USART_STAT0_RBNE) != 0;
            if (residual) {
                byte = usart_data_receive(c->reg);
            }
            if (residual && d->rx_count < d->rx_size) {
                d->rx[d->rx_count++] = byte;
                residual = false;
            }
        }
        if (residual && !d->next) {
            struct uart_event e = {
                .type = UART_RX_STOPPED, .data.rx_stop.reason = UART_ERROR_OVERRUN};
            emit(dev, &e);
        }
        rx_report(dev);
        if (residual && d->next) {
            d->next[0] = byte;
            struct uart_event e = {
                .type = UART_RX_RDY, .data.rx = {.buf = d->next, .offset = 0, .len = 1}};
            emit(dev, &e);
        }
        release_rx(dev);
    } else {
        d->rx_stopping = false;
    }
    irq_unlock(key);
    return err;
}

static int async_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
    struct gd32_usart_data *d = dev->data;
    unsigned int key = irq_lock();
    int err = !d->rx || d->rx_stopping ? -EACCES : (d->next ? -EBUSY : 0);
    if (!buf || !len || len > UINT16_MAX || buf == d->rx) {
        err = -EINVAL;
    }
    if (!err) {
        d->next = buf;
        d->next_size = len;
    }
    irq_unlock(key);
    return err;
}

static int async_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout)
{
    const struct gd32_usart_config *c = dev->config;
    struct gd32_usart_data *d = dev->data;
    if (!buf || !len || len > UINT16_MAX || !d->callback ||
        (timeout < 0 && timeout != SYS_FOREVER_US)) {
        return -EINVAL;
    }
    uint64_t bits = timeout > 0 ? DIV_ROUND_UP((uint64_t)timeout * d->baud_rate, 1000000) : 0;
    if ((!c->rx_dma && timeout != 0) || bits > 0xfffffe ||
        (c->rx_dma && c->reg != USART0 && c->reg != USART1 && c->reg != USART2 &&
            c->reg != USART5)) {
        return -ENOTSUP;
    }
    unsigned int key = irq_lock();
    if (d->rx || d->rx_stopping) {
        irq_unlock(key);
        return -EBUSY;
    }
    rx_interrupts_off(dev);
    d->rx = buf;
    d->rx_size = len;
    d->rx_count = d->rx_offset = 0;
    d->next = NULL;
    d->rx_timeout = timeout;
    d->rx_epoch++;
    if (c->rx_dma) {
        /* 已关闭期间的字节不归新会话，禁止作为下一帧前缀重新交付。 */
        usart_dma_receive_config(c->reg, USART_DENR_DISABLE);
        gd32_usart_clear_rx(c->reg);
    }
    int err = c->rx_dma ? rx_dma_resume(dev) : 0;
    if (err) {
        d->rx = NULL;
        irq_unlock(key);
        return err;
    }
    if (c->rx_dma) {
        /* 不开启 RBNE 中断，数据搬运由 DMA 独占。 */
        if (timeout != SYS_FOREVER_US) {
            /* 微秒超时向上取整；0 使用硬件最小的一个波特时钟。 */
            usart_receiver_timeout_threshold_config(c->reg, MAX(bits, 1));
            gd32_usart_flag_clear(c->reg, RT);
            usart_receiver_timeout_enable(c->reg);
            gd32_usart_interrupt_enable(c->reg, RT);
        }
    } else {
        usart_gd32_irq_rx_enable(dev);
    }
    usart_gd32_irq_err_enable(dev);
    struct uart_event e = {.type = UART_RX_BUF_REQUEST};
    emit(dev, &e);
    irq_unlock(key);
    return err;
}

static void async_rx_isr(const struct device *dev)
{
    struct gd32_usart_data *d = dev->data;
    const struct gd32_usart_config *c = dev->config;
    if (!d->rx || d->rx_stopping) {
        return;
    }
    uint32_t stat = gd32_usart_status(c->reg);
    int errors = rx_errors(stat);
    if (errors) {
        rx_failed(dev, errors);
    } else if (c->rx_dma) {
        if (d->rx_timeout != SYS_FOREVER_US && gd32_usart_interrupt_flag_get(c->reg, RT)) {
            gd32_usart_flag_clear(c->reg, RT);
            rx_dma_service(dev);
        }
    } else if (stat & USART_STAT0_RBNE) {
        uint32_t epoch = d->rx_epoch;
        d->rx[d->rx_count++] = usart_data_receive(c->reg);
        rx_report(dev);
        if (d->rx_epoch == epoch && d->rx && d->rx_count == d->rx_size) {
            rx_full(dev, false);
        }
    }
}
