#include "internal.h"
#include <modbus_support/rtu_frame.h>
#include <errno.h>
#include <string.h>

/* 栈上登记检查回调，允许不同线程并发 check，拒绝同线程跨会话递归。 */
typedef struct tr_check_call {
    k_tid_t thread;
    struct tr_check_call *next;
} tr_check_call_t;

struct tr_engine {
    tr_engine_config_t config;
    mb_server_t *modbus_server;
    struct k_mutex execution_lock;
    atomic_ptr_t executing_thread;
    struct k_spinlock check_lock;
    tr_check_call_t *checks;
    tr_session_t *sessions;
    uint32_t gap_us;
};

struct tr_session {
    tr_engine_t *engine;
    tr_session_t *next;
    tr_session_config_t config;
    atomic_t busy;
    uint64_t *received;
    uint8_t *rx;
    uint8_t *tx;
    size_t size;
    size_t protocol;
    uint32_t gap_us;
    uint64_t now_us;
    uint64_t last_rx_us;
    bool have_rx;
    bool at_boundary;
    bool damaged;
};

static void consume(tr_session_t *session, size_t count)
{
    size_t remaining = session->size - count;
    if (remaining) {
        session->at_boundary =
            session->received[count] - session->received[count - 1] >= session->gap_us;
    } else {
        session->at_boundary = false;
    }
    memmove(session->rx, session->rx + count, remaining);
    memmove(session->received, session->received + count, remaining * sizeof(uint64_t));
    session->size = remaining;
}

static tr_view_t view_for(tr_session_t *session, const tr_protocol_t *protocol)
{
    tr_view_t view = {
        .data = session->rx,
        .size = session->size,
        .at_boundary = session->at_boundary,
        .expired = session->now_us - session->received[0] >= protocol->timeout_us,
    };
    uint32_t gap = session->gap_us;
    for (size_t i = 1; i < session->size; ++i) {
        uint64_t elapsed = session->received[i] - session->received[i - 1];
        if (elapsed >= gap) {
            view.boundary_size = i;
            break;
        }
        if (elapsed > (uint64_t)gap * 3 / 7) {
            view.broken = true;
        }
    }
    if (!view.boundary_size && session->now_us - session->last_rx_us >= gap) {
        view.boundary_size = session->size;
    }
    return view;
}

static tr_match_t check_protocol(
    tr_session_t *session, const tr_protocol_t *protocol, const tr_view_t *view, size_t *consumed)
{
    if (protocol->type == TR_PROTOCOL_MODBUS) {
        int err = mb_rtu_probe(view->data,
            view->size,
            view->boundary_size,
            view->at_boundary,
            view->broken,
            view->expired,
            consumed);
        return !err ? TR_COMPLETE : err == -EAGAIN ? TR_WAIT : TR_MISMATCH;
    }
    tr_engine_t *engine = session->engine;
    tr_check_call_t call = {.thread = k_current_get()};
    k_spinlock_key_t key = k_spin_lock(&engine->check_lock);
    call.next = engine->checks;
    engine->checks = &call;
    k_spin_unlock(&engine->check_lock, key);
    tr_match_t match = protocol->custom.check(
        protocol->custom.context, session->config.custom_context, view, consumed);
    key = k_spin_lock(&engine->check_lock);
    tr_check_call_t **entry = &engine->checks;
    while (*entry != &call) {
        entry = &(*entry)->next;
    }
    *entry = call.next;
    k_spin_unlock(&engine->check_lock, key);
    return match;
}

static int process(tr_session_t *session)
{
    tr_engine_t *engine = session->engine;
    const tr_engine_config_t *config = &engine->config;
    size_t tried = 0;
    int result = 0;
    while (session->size) {
        const tr_protocol_t *protocol = &config->protocols[session->protocol];
        tr_view_t view = view_for(session, protocol);
        size_t count = 0;
        tr_match_t match = check_protocol(session, protocol, &view, &count);
        if (match == TR_WAIT && !view.expired &&
            session->size <= MIN(protocol->max_frame, session->config.max_frame)) {
            break;
        }
        if (match == TR_COMPLETE && count && count <= session->size &&
            count <= MIN(protocol->max_frame, session->config.max_frame)) {
            size_t response_size = 0;
            int err = k_mutex_lock(&engine->execution_lock, K_FOREVER);
            if (err) {
                return err;
            }
            atomic_ptr_set(&engine->executing_thread, k_current_get());
            if (protocol->type == TR_PROTOCOL_MODBUS) {
                err = tr_modbus_execute(engine->modbus_server,
                    &protocol->modbus,
                    session->config.channel_id,
                    session->config.allow_routes,
                    session->rx,
                    count,
                    session->tx,
                    session->config.tx_capacity,
                    &response_size);
            } else {
                err = protocol->custom.execute(protocol->custom.context,
                    session->config.custom_context,
                    session->rx,
                    count,
                    session->tx,
                    session->config.tx_capacity,
                    &response_size);
            }
            atomic_ptr_clear(&engine->executing_thread);
            k_mutex_unlock(&engine->execution_lock);
            /* 一经选择即消费，业务失败或发送失败都不重放。 */
            consume(session, count);
            tried = 0;
            if (!err && response_size) {
                err = response_size > session->config.tx_capacity
                          ? -EMSGSIZE
                          : vport_output(config->port,
                                session->config.channel_id,
                                session->tx,
                                response_size,
                                K_MSEC(config->send_timeout_ms));
            }
            if (err && !result) {
                result = err;
            }
            continue;
        }
        session->protocol = (session->protocol + 1) % config->protocol_count;
        if (++tried == config->protocol_count) {
            consume(session, 1);
            tried = 0;
        }
    }
    return result;
}

static int receive_on_session(tr_session_t *session, const vport_event_t *event)
{
    if (event->at_us < session->now_us) {
        return -EINVAL;
    }
    session->now_us = event->at_us;
    if (event->type == VPORT_ERROR) {
        session->size = 0;
        session->protocol = 0;
        session->damaged = true;
        session->last_rx_us = event->at_us;
        session->have_rx = true;
        return -EIO;
    }
    if (event->type != VPORT_DATA) {
        if (session->damaged && event->at_us - session->last_rx_us >= session->gap_us) {
            session->damaged = false;
        }
        return process(session);
    }
    bool start = !session->have_rx || event->at_us - session->last_rx_us >= session->gap_us;
    session->last_rx_us = event->at_us;
    session->have_rx = true;
    if (session->damaged && !start) {
        return -EIO;
    }
    session->damaged = false;
    if (event->size > session->config.rx_capacity - session->size) {
        session->size = 0;
        session->protocol = 0;
        session->damaged = true;
        return -ENOBUFS;
    }
    if (!session->size) {
        session->at_boundary = start;
    }
    memcpy(session->rx + session->size, event->data, event->size);
    for (size_t i = 0; i < event->size; ++i) {
        session->received[session->size + i] = event->at_us;
    }
    session->size += event->size;
    return process(session);
}

static int receive(void *context, const vport_event_t *event)
{
    tr_session_t *session = context;
    if (event->channel_id != session->config.channel_id) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&session->engine->check_lock);
    bool checking = false;
    for (tr_check_call_t *call = session->engine->checks; call; call = call->next) {
        if (call->thread == k_current_get()) {
            checking = true;
            break;
        }
    }
    k_spin_unlock(&session->engine->check_lock, key);
    /* 检查和执行回调均禁止同线程跨会话递归，其他线程仍可独立组帧。 */
    if (checking || atomic_ptr_get(&session->engine->executing_thread) == k_current_get() ||
        !atomic_cas(&session->busy, 0, 1)) {
        return -EBUSY;
    }
    int err = 0;
    if (!session->config.accept || session->config.accept(session->config.accept_context, event)) {
        err = receive_on_session(session, event);
    }
    atomic_clear(&session->busy);
    return err;
}

int tr_engine_create(const tr_engine_config_t *config, tr_engine_t **out)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!out) {
        return -EINVAL;
    }
    *out = NULL;
    if (!config || !config->port || !config->protocols || !config->protocol_count ||
        !config->send_timeout_ms) {
        return -EINVAL;
    }
    if (config->port->state != VPORT_INIT) {
        return -EBUSY;
    }
    unsigned types = 0;
    uint32_t gap = 1;
    for (size_t i = 0; i < config->protocol_count; ++i) {
        const tr_protocol_t *protocol = &config->protocols[i];
        if ((protocol->type != TR_PROTOCOL_MODBUS && protocol->type != TR_PROTOCOL_CUSTOM) ||
            !protocol->timeout_us || !protocol->max_frame) {
            return -EINVAL;
        }
        if (types & BIT(protocol->type)) {
            return -EEXIST;
        }
        types |= BIT(protocol->type);
        if (protocol->type == TR_PROTOCOL_MODBUS) {
            int err = tr_modbus_validate(&protocol->modbus);
            if (err) {
                return err;
            }
            gap = protocol->modbus.gap_us;
            if (protocol->timeout_us < gap || protocol->max_frame < 4 ||
                protocol->max_frame > sizeof(((struct modbus_adu *)0)->data) + 4U ||
                (protocol->max_frame > 256 && !protocol->modbus.route_count)) {
                return -EINVAL;
            }
        } else if (!protocol->custom.check || !protocol->custom.execute) {
            return -EINVAL;
        }
    }
    tr_engine_t *engine = k_calloc(1, sizeof(*engine));
    if (!engine) {
        return -ENOMEM;
    }
    engine->config = *config;
    engine->gap_us = gap;
    k_mutex_init(&engine->execution_lock);
    for (size_t i = 0; i < config->protocol_count; ++i) {
        if (config->protocols[i].type == TR_PROTOCOL_MODBUS) {
            int err = mb_server_open(config->protocols[i].modbus.unit, &engine->modbus_server);
            if (err) {
                k_free(engine);
                return err;
            }
        }
    }
    *out = engine;
    return 0;
}

int tr_session_create(tr_engine_t *engine, const tr_session_config_t *config, tr_session_t **out)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!out) {
        return -EINVAL;
    }
    *out = NULL;
    if (!engine || !config || !config->channel_id || !config->rx_capacity || !config->tx_capacity ||
        !config->max_frame || config->max_frame > config->rx_capacity) {
        return -EINVAL;
    }
    if (engine->config.port->state != VPORT_INIT) {
        return -EBUSY;
    }
    for (tr_session_t *session = engine->sessions; session; session = session->next) {
        if (session->config.channel_id == config->channel_id) {
            return -EALREADY;
        }
    }
    for (size_t i = 0; i < engine->config.protocol_count; ++i) {
        const tr_protocol_t *protocol = &engine->config.protocols[i];
        if (protocol->type == TR_PROTOCOL_MODBUS &&
            (config->tx_capacity < 256 || config->max_frame < 4 ||
                (config->allow_routes && protocol->modbus.route_count &&
                    config->max_frame < protocol->max_frame))) {
            return -EINVAL;
        }
    }
    size_t fixed = sizeof(tr_session_t);
    if (config->tx_capacity > SIZE_MAX - fixed) {
        return -EOVERFLOW;
    }
    fixed += config->tx_capacity;
    if (config->rx_capacity > (SIZE_MAX - fixed) / (sizeof(uint64_t) + 1)) {
        return -EOVERFLOW;
    }
    tr_session_t *session = k_calloc(1, fixed + config->rx_capacity * (sizeof(uint64_t) + 1));
    if (!session) {
        return -ENOMEM;
    }
    session->engine = engine;
    session->config = *config;
    session->gap_us = engine->gap_us;
    session->received = (uint64_t *)(session + 1);
    session->rx = (uint8_t *)(session->received + config->rx_capacity);
    session->tx = session->rx + config->rx_capacity;
    int err = vport_channel_bind(engine->config.port, config->channel_id, receive, session);
    if (err) {
        k_free(session);
        return err;
    }
    session->next = engine->sessions;
    engine->sessions = session;
    *out = session;
    return 0;
}

int tr_session_destroy(tr_session_t *session)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!session) {
        return -EINVAL;
    }
    if (!atomic_cas(&session->busy, 0, 1)) {
        return -EBUSY;
    }
    int err = vport_channel_unbind(session->engine->config.port, session->config.channel_id);
    if (err) {
        atomic_clear(&session->busy);
        return err;
    }
    tr_session_t **cursor = &session->engine->sessions;
    while (*cursor != session) {
        cursor = &(*cursor)->next;
    }
    *cursor = session->next;
    k_free(session);
    return 0;
}

int tr_engine_destroy(tr_engine_t *engine)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!engine) {
        return -EINVAL;
    }
    vport_state_t state = engine->config.port->state;
    if (engine->sessions || (state != VPORT_INIT && state != VPORT_STOPPED)) {
        return -EBUSY;
    }
    if (engine->modbus_server) {
        int err = mb_server_close(engine->modbus_server);
        if (err) {
            return err;
        }
    }
    k_free(engine);
    return 0;
}
