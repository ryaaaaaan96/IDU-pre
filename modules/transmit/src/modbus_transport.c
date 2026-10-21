#include <transmit/modbus_transport.h>
#include "transport_hooks.h"
#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include "modbus_internal.h"

BUILD_ASSERT(CONFIG_VPORT_MAX_PAYLOAD >= TR_MODBUS_TRANSPORT_MTU,
             "vport must hold a complete Modbus ADU and handoff header");

struct endpoint_state {
    const struct tr_modbus_transport *config;
    int iface;
    atomic_t raw_busy;
    struct k_sem received, transmitted;
    struct k_spinlock rx_lock;
    uint32_t generation;
    bool waiting;
    struct modbus_adu *destination;
    k_tid_t sender;
    atomic_t transmitting;
};

static struct endpoint_state states[CONFIG_TR_MODBUS_TRANSPORT_PORTS];
static size_t endpoint_count;
K_MUTEX_DEFINE(bind_lock);

static struct endpoint_state *by_iface(struct modbus_context *ctx)
{
    int iface = modbus_iface_get_by_ctx(ctx);
    for (size_t i = 0; i < endpoint_count; ++i) {
        if (states[i].iface == iface) { return &states[i]; }
    }
    return NULL;
}

static struct endpoint_state *by_config(const void *config)
{
    for (size_t i = 0; i < endpoint_count; ++i) {
        if (states[i].config == config) { return &states[i]; }
    }
    return NULL;
}

int tr_modbus_transport_bind(const struct tr_modbus_transport *endpoints, size_t count)
{
    if (!endpoints || !count || count > ARRAY_SIZE(states) || k_is_in_isr()) {
        return -EINVAL;
    }
    k_mutex_lock(&bind_lock, K_FOREVER);
    int err = endpoint_count ? -EALREADY : 0;
    for (size_t i = 0; !err && i < count; ++i) {
        if (!endpoints[i].iface_name) { err = -EINVAL; break; }
        int iface = modbus_iface_get_by_name(endpoints[i].iface_name);
        if (iface < 0) { err = iface; break; }
        for (size_t j = 0; j < i; ++j) {
            if (states[j].iface == iface || endpoints[j].link == endpoints[i].link ||
                endpoints[j].port == endpoints[i].port) { err = -EEXIST; break; }
        }
        states[i].iface = iface;
        states[i].config = &endpoints[i];
        k_sem_init(&states[i].received, 0, 1);
        k_sem_init(&states[i].transmitted, 0, 1);
    }
    if (!err) { endpoint_count = count; }
    k_mutex_unlock(&bind_lock);
    return err;
}

static int encode(const struct modbus_adu *adu, uint32_t generation, uint8_t *data)
{
    if (adu->length > sizeof(adu->data)) { return -EMSGSIZE; }
    sys_put_be32(generation, data);
    sys_put_be16(adu->trans_id, data + 4);
    sys_put_be16(adu->proto_id, data + 6);
    sys_put_be16(adu->length + 2, data + 8);
    data[10] = adu->unit_id;
    data[11] = adu->fc;
    memcpy(data + 12, adu->data, adu->length);
    return 12 + adu->length;
}

static int decode(const struct vport_frame *frame, struct modbus_adu *adu)
{
    if (!frame || !frame->data || frame->size < 12 || frame->id || frame->flags) {
        return -EINVAL;
    }
    uint16_t length = sys_get_be16(frame->data + 8);
    if (length < 2 || length - 2 > sizeof(adu->data) ||
        frame->size != (size_t)length + 10) { return -EMSGSIZE; }
    *adu = (struct modbus_adu){
        .trans_id = sys_get_be16(frame->data + 4),
        .proto_id = sys_get_be16(frame->data + 6),
        .length = length - 2, .unit_id = frame->data[10], .fc = frame->data[11],
        .crc = adu->crc, /* 一对一交接保留串口层已经验证的 CRC 元数据。 */
    };
    memcpy(adu->data, frame->data + 12, adu->length);
    return 0;
}

int tr_modbus_transport_rx_begin(struct modbus_context *ctx)
{
    struct endpoint_state *state = by_iface(ctx);
    if (!state) { return 0; }
    /* RAW submit 原本直接覆盖 rx_adu；交接及业务处理期间必须拒绝第二帧。 */
    if (!atomic_cas(&state->raw_busy, 0, 1)) {
        vport_rx_gap(state->config->link, 0);
        return -EBUSY;
    }
    return 0;
}

void tr_modbus_transport_rx_end(struct modbus_context *ctx)
{
    struct endpoint_state *state = by_iface(ctx);
    if (state) { atomic_clear(&state->raw_busy); }
}

void tr_modbus_transport_receive(const struct tr_modbus_transport *endpoint,
                                 const struct vport_source *source,
                                 const struct vport_frame *frame)
{
    struct endpoint_state *state = by_config(endpoint);
    if (!state || !source || source->port != state->config->port ||
        source->link != state->config->link || source->peer ||
        !frame || !frame->data || frame->size < 12) { return; }
    k_spinlock_key_t key = k_spin_lock(&state->rx_lock);
    /* 超时后的旧队列项不能成为下一次请求的输入。 */
    if (state->waiting && sys_get_be32(frame->data) == state->generation &&
        !decode(frame, state->destination)) {
        state->waiting = false;
        k_sem_give(&state->received);
    }
    k_spin_unlock(&state->rx_lock, key);
}

int tr_modbus_transport_rx_route(struct modbus_context *ctx)
{
    struct endpoint_state *state = by_iface(ctx);
    if (!state) { return 0; }
    uint8_t data[TR_MODBUS_TRANSPORT_MTU];
    k_spinlock_key_t key = k_spin_lock(&state->rx_lock);
    uint32_t generation = ++state->generation;
    state->waiting = true;
    state->destination = &ctx->rx_adu;
    k_sem_reset(&state->received);
    k_spin_unlock(&state->rx_lock, key);
    int size = encode(&ctx->rx_adu, generation, data);
    struct vport_frame frame = {.data = data, .size = size > 0 ? (size_t)size : 0};
    int err = size < 0 ? size : vport_rx_submit(state->config->link, 0, &frame);
    if (!err && k_sem_take(&state->received, K_MSEC(CONFIG_TR_MODBUS_TRANSPORT_RX_TIMEOUT_MS))) {
        err = -ETIMEDOUT;
    }
    key = k_spin_lock(&state->rx_lock);
    state->waiting = false;
    state->destination = NULL;
    k_spin_unlock(&state->rx_lock, key);
    return err;
}

void tr_modbus_transport_tx_done(struct modbus_context *ctx)
{
    struct endpoint_state *state = by_iface(ctx);
    if (state && atomic_get(&state->transmitting)) { k_sem_give(&state->transmitted); }
}

int tr_modbus_transport_send(const struct tr_modbus_transport *endpoint, uint16_t port,
                             uint64_t peer, const struct vport_frame *frame,
                             k_timeout_t timeout)
{
    struct endpoint_state *state = by_config(endpoint);
    if (!state || port != state->config->port || peer ||
        k_is_in_isr() || state->sender != k_current_get()) { return -EACCES; }
    struct modbus_context *ctx = modbus_get_context(state->iface);
    if (!ctx) { return -ENODEV; }
    int err = decode(frame, &ctx->tx_adu);
    if (err) { return err; }
    if (ctx->mode == MODBUS_MODE_RAW) {
        /* RAW 后端须同步消费/复制 ADU，返回后不能再持有该指针。 */
        return ctx->rawcb.raw_tx_cb(state->iface, &ctx->tx_adu, ctx->rawcb.user_data);
    }
#ifdef CONFIG_MODBUS_SERIAL
    if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) { return -ETIMEDOUT; }
    k_sem_reset(&state->transmitted);
    atomic_set(&state->transmitting, 1);
    err = modbus_serial_tx_adu(ctx);
    if (!err && k_sem_take(&state->transmitted, timeout)) { err = -ETIMEDOUT; }
    atomic_clear(&state->transmitting);
    if (err) {
        /* 返回前关 TX/DE、清掉未发送缓冲并恢复 RX，不留下悬挂发送。 */
        tr_modbus_transport_serial_abort(ctx);
    }
    return err;
#else
    return -ENOTSUP;
#endif
}

bool tr_modbus_transport_tx_route(struct modbus_context *ctx, int *error)
{
    struct endpoint_state *state = by_iface(ctx);
    if (!state) { return false; }
    uint8_t data[TR_MODBUS_TRANSPORT_MTU];
    int size = encode(&ctx->tx_adu, 0, data);
    *error = size;
    if (size >= 0) {
        const struct vport_frame frame = {.data = data, .size = (size_t)size};
        state->sender = k_current_get();
        *error = vport_send(state->config->port, state->config->link, 0, &frame,
                            K_MSEC(CONFIG_TR_MODBUS_TRANSPORT_TX_TIMEOUT_MS));
        state->sender = NULL;
    }
#ifdef CONFIG_MODBUS_SERIAL
    /* 路由拒绝/锁超时也必须恢复接收；这时后端还没有机会执行清理。 */
    if (*error && ctx->mode != MODBUS_MODE_RAW &&
        !atomic_test_bit(&ctx->state, MODBUS_STATE_RX_ENABLED)) {
        tr_modbus_transport_serial_abort(ctx);
    }
#endif
    return true;
}
