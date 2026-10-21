int mb_serial_open(const char *name, struct modbus_serial_param param,
                   mb_serial_receive_fn receive, void *user)
{
#ifdef CONFIG_MODBUS_SERIAL
    if (!name || !receive || k_is_in_isr()) { return -EINVAL; }
    int iface = modbus_iface_get_by_name(name);
    if (iface < 0) { return iface; }
    struct modbus_context *ctx = &mb_ctx_tbl[iface];
    if (!ctx->cfg) { return -EINVAL; }
    struct mb_io_state *io = &mb_io[iface];
    if (io->receive) { return io->receive == receive && io->user == user ? iface : -EALREADY; }
    for (size_t i = 0; i < ARRAY_SIZE(mb_ctx_tbl); ++i) {
        if (mb_ctx_tbl[i].cfg && mb_ctx_tbl[i].cfg->dev == ctx->cfg->dev &&
            atomic_test_bit(&mb_ctx_tbl[i].state, MODBUS_STATE_CONFIGURED)) { return -EALREADY; }
    }
    ctx = modbus_init_iface(iface);
    if (!ctx) { return -EALREADY; }
    k_mutex_init(&io->lock); k_sem_init(&io->transmitted, 0, 1);
    io->next_tx = sys_timepoint_calc(K_NO_WAIT);
    io->user = user; io->receive = receive; io->param = param;
    ctx->client = false; /* Receive continuously; dispatch bypasses the server parser. */
    struct modbus_iface_param settings = {.mode = MODBUS_MODE_RTU, .serial = param};
    int err = modbus_serial_init(ctx, settings);
    if (err) {
        modbus_serial_disable(ctx); io->receive = NULL;
        atomic_clear_bit(&ctx->state, MODBUS_STATE_CONFIGURED);
        return err;
    }
    return iface;
#else
    return -ENOTSUP;
#endif
}
