/* Private implementation inserted after Zephyr's interface table. */
#include <zephyr/sys/crc.h>
struct mb_io_state {
    atomic_t raw_busy;
    void (*done)(void *user);
    void *done_user;
#ifdef CONFIG_MODBUS_SERIAL
    mb_serial_receive_fn receive;
    void *user;
    struct k_mutex lock;
    struct k_sem transmitted;
    atomic_t transmitting, pending, overflow;
    struct modbus_serial_param param;
    k_timepoint_t next_tx;
#endif
};
static struct mb_io_state mb_io[ARRAY_SIZE(mb_ctx_tbl)];

int mb_raw_rx_begin(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    if (iface < 0) { return iface; }
    /* Direct Zephyr callers retain their own scheduling contract. Bound message
     * ports use the completion hook to hold ownership through protocol work. */
    if (!mb_io[iface].done) { return 0; }
    return atomic_cas(&mb_io[iface].raw_busy, 0, 1) ? 0 : -EBUSY;
}
void mb_raw_rx_end(struct modbus_context *ctx)
{
    if (ctx->mode != MODBUS_MODE_RAW) { return; }
    int iface = modbus_iface_get_by_ctx(ctx);
    struct mb_io_state *io = &mb_io[iface];
    if (io->done) { io->done(io->done_user); }
    atomic_clear(&io->raw_busy);
}
int mb_raw_set_done(int iface, void (*done)(void *user), void *user)
{
    if (iface < 0 || iface >= ARRAY_SIZE(mb_io) || mb_ctx_tbl[iface].cfg) { return -EINVAL; }
    if (!done) {
        if (mb_io[iface].done_user != user || atomic_get(&mb_io[iface].raw_busy)) { return -EBUSY; }
        mb_io[iface].done = NULL; mb_io[iface].done_user = NULL;
        return 0;
    }
    if (mb_io[iface].done && (mb_io[iface].done != done || mb_io[iface].done_user != user)) {
        return -EALREADY;
    }
    mb_io[iface].done = done; mb_io[iface].done_user = user;
    return 0;
}

bool mb_serial_dispatch(struct modbus_context *ctx)
{
#ifdef CONFIG_MODBUS_SERIAL
    struct mb_io_state *io = &mb_io[modbus_iface_get_by_ctx(ctx)];
    if (atomic_set(&io->overflow, 0)) { ctx->rx_adu_err = -EMSGSIZE; }
    if (!io->receive) { return false; }
    int err = ctx->rx_adu_err;
    if (!err) {
        /* Parser resets the counter but leaves the complete wire frame intact.
         * The receiver must copy before returning; avoid another large stack buffer. */
        const uint8_t *data = ctx->cfg->uart_buf;
        atomic_set(&io->pending, 1);
        err = io->receive(io->user, data, ctx->rx_adu.length + 4);
    }
    if (err) {
        atomic_clear(&io->pending);
        modbus_serial_rx_enable(ctx);
    }
    return true;
#else
    return false;
#endif
}
void mb_serial_tx_done(struct modbus_context *ctx)
{
#ifdef CONFIG_MODBUS_SERIAL
    struct mb_io_state *io = &mb_io[modbus_iface_get_by_ctx(ctx)];
    if (io->receive && atomic_get(&io->transmitting)) {
        /* A server request owns RX until its RAW handler has fully finished. */
        if (atomic_get(&io->pending)) { modbus_serial_rx_disable(ctx); }
        k_sem_give(&io->transmitted);
    }
#endif
}
void mb_serial_release(int iface)
{
#ifdef CONFIG_MODBUS_SERIAL
    if (iface < 0 || iface >= ARRAY_SIZE(mb_io) || !mb_io[iface].receive) { return; }
    atomic_clear(&mb_io[iface].pending);
    if (!atomic_get(&mb_io[iface].transmitting)) { modbus_serial_rx_enable(&mb_ctx_tbl[iface]); }
#endif
}
int mb_serial_send(int iface, const uint8_t *data, size_t size, k_timeout_t timeout)
{
#ifdef CONFIG_MODBUS_SERIAL
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (iface < 0 || iface >= ARRAY_SIZE(mb_io) || !mb_io[iface].receive) { return -ENODEV; }
    if (!data || size < 4 || size > CONFIG_MODBUS_BUFFER_SIZE) { return -EMSGSIZE; }
    if (crc16_ansi(data, size - 2) != sys_get_le16(data + size - 2)) { return -EBADMSG; }
    struct mb_io_state *io = &mb_io[iface];
    struct modbus_context *ctx = &mb_ctx_tbl[iface];
    k_timepoint_t deadline = sys_timepoint_calc(timeout);
    int err = k_mutex_lock(&io->lock, sys_timepoint_timeout(deadline));
    if (err) { return err; }
    /* Shared links may start a client transaction while RX is assembling a
     * frame. Never overwrite that buffer; wait for its dispatch and TX silence.
     * This coordinates this MCU only, not other masters on the wire.
     */
    for (;;) {
        if (sys_timepoint_expired(deadline)) { err = -ETIMEDOUT; goto out; }
        unsigned int key = irq_lock();
        bool idle = !ctx->cfg->uart_buf_ctr && !k_work_busy_get(&ctx->server_work) &&
                    sys_timepoint_expired(io->next_tx);
        if (idle) {
            atomic_set(&io->transmitting, 1);
            modbus_serial_rx_disable(ctx);
        }
        irq_unlock(key);
        if (idle) { break; }
        k_sleep(K_TICKS(1));
    }
    ctx->tx_adu.unit_id = data[0]; ctx->tx_adu.fc = data[1];
    ctx->tx_adu.length = size - 4;
    memcpy(ctx->tx_adu.data, data + 2, size - 4);
    k_sem_reset(&io->transmitted);
    err = modbus_serial_tx_adu(ctx);
    if (!err && k_sem_take(&io->transmitted, sys_timepoint_timeout(deadline))) { err = -ETIMEDOUT; }
    if (err) { mb_serial_abort(ctx); }
    /* Reuse the serial backend's configured RTU inter-frame delay. */
    io->next_tx = sys_timepoint_calc(K_USEC(ctx->cfg->rtu_timeout));
    atomic_clear(&io->transmitting);
out:
    k_mutex_unlock(&io->lock);
    return err;
#else
    return -ENOTSUP;
#endif
}
int mb_serial_configure(int iface, struct modbus_serial_param param)
{
#ifdef CONFIG_MODBUS_SERIAL
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (iface < 0 || iface >= ARRAY_SIZE(mb_io) || !mb_io[iface].receive) { return -ENODEV; }
    struct mb_io_state *io = &mb_io[iface];
    struct modbus_context *ctx = &mb_ctx_tbl[iface];
    k_mutex_lock(&io->lock, K_FOREVER);
    int err = 0;
    if (io->param.baud == param.baud && io->param.parity == param.parity &&
        io->param.stop_bits == param.stop_bits) { goto out; }
    unsigned int key = irq_lock();
    if (atomic_get(&io->pending) || k_work_busy_get(&ctx->server_work) || ctx->cfg->uart_buf_ctr) {
        irq_unlock(key); err = -EBUSY; goto out;
    }
    modbus_serial_disable(ctx);
    irq_unlock(key);
    struct modbus_iface_param settings = {.mode = MODBUS_MODE_RTU, .serial = param};
    err = modbus_serial_init(ctx, settings);
    if (!err) { io->param = param; }
    else {
        settings.serial = io->param;
        if (modbus_serial_init(ctx, settings)) { err = -ENOTRECOVERABLE; }
    }
out:
    k_mutex_unlock(&io->lock);
    return err;
#else
    return -ENOTSUP;
#endif
}

void mb_serial_overflow(struct modbus_context *ctx)
{
#ifdef CONFIG_MODBUS_SERIAL
    atomic_set(&mb_io[modbus_iface_get_by_ctx(ctx)].overflow, 1);
#endif
}
