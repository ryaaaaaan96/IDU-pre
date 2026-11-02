#include "internal.h"
#include <modbus_support/rtu_frame.h>
#include <errno.h>
#include <string.h>

/* 单实例由 APP 串行调用，不引入锁或模块线程。 */
typedef struct {
    const tr_config_t *config;
    mb_server_t *modbus_server;
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
} tr_instance_t;

static void consume(tr_instance_t *instance, size_t count)
{
    size_t remaining = instance->size - count;
    if (remaining) {
        instance->at_boundary =
            instance->received[count] - instance->received[count - 1] >= instance->gap_us;
    } else {
        instance->at_boundary = false;
    }
    memmove(instance->rx, instance->rx + count, remaining);
    memmove(instance->received, instance->received + count, remaining * sizeof(uint64_t));
    instance->size = remaining;
}

static tr_view_t view_for(tr_instance_t *instance, const tr_protocol_t *protocol)
{
    tr_view_t view = {
        .data = instance->rx,
        .size = instance->size,
        .at_boundary = instance->at_boundary,
        .expired = instance->now_us - instance->received[0] >= protocol->timeout_us,
    };
    uint32_t gap = instance->gap_us;
    for (size_t i = 1; i < instance->size; ++i) {
        uint64_t elapsed = instance->received[i] - instance->received[i - 1];
        if (elapsed >= gap) {
            view.boundary_size = i;
            break;
        }
        if (elapsed > (uint64_t)gap * 3 / 7) {
            view.broken = true;
        }
    }
    if (!view.boundary_size && instance->now_us - instance->last_rx_us >= gap) {
        view.boundary_size = instance->size;
    }
    return view;
}

static tr_match_t check_protocol(
    const tr_protocol_t *protocol, const tr_view_t *view, size_t *consumed)
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
    return protocol->custom.check(protocol->custom.context, view, consumed);
}

static int process(tr_instance_t *instance)
{
    const tr_config_t *config = instance->config;
    size_t tried = 0;
    int result = 0;
    while (instance->size) {
        const tr_protocol_t *protocol = &config->protocols[instance->protocol];
        tr_view_t view = view_for(instance, protocol);
        size_t count = 0;
        tr_match_t match = check_protocol(protocol, &view, &count);
        if (match == TR_WAIT && !view.expired && instance->size <= protocol->max_frame) {
            break;
        }
        if (match == TR_COMPLETE && count && count <= instance->size &&
            count <= protocol->max_frame) {
            size_t response_size = 0;
            int err;
            if (protocol->type == TR_PROTOCOL_MODBUS) {
                err = tr_modbus_execute(instance->modbus_server,
                    &protocol->modbus,
                    instance->rx,
                    count,
                    instance->tx,
                    config->tx_capacity,
                    &response_size);
            } else {
                err = protocol->custom.execute(protocol->custom.context,
                    instance->rx,
                    count,
                    instance->tx,
                    config->tx_capacity,
                    &response_size);
            }
            /* 一经选择即消费，业务失败或发送失败都不重放。 */
            consume(instance, count);
            tried = 0;
            if (!err && response_size) {
                err = response_size > config->tx_capacity ? -EMSGSIZE
                                                          : vport_output(config->port,
                                                                instance->tx,
                                                                response_size,
                                                                K_MSEC(config->send_timeout_ms));
            }
            if (err && !result) {
                result = err;
            }
            continue;
        }
        instance->protocol = (instance->protocol + 1) % config->protocol_count;
        if (++tried == config->protocol_count) {
            consume(instance, 1);
            tried = 0;
        }
    }
    return result;
}

static int receive(void *context, const vport_event_t *event)
{
    tr_instance_t *instance = context;
    if (event->at_us < instance->now_us) {
        return -EINVAL;
    }
    instance->now_us = event->at_us;
    if (event->type == VPORT_ERROR) {
        instance->size = 0;
        instance->protocol = 0;
        instance->damaged = true;
        instance->last_rx_us = event->at_us;
        instance->have_rx = true;
        return -EIO;
    }
    if (event->type != VPORT_DATA) {
        if (instance->damaged && event->at_us - instance->last_rx_us >= instance->gap_us) {
            instance->damaged = false;
        }
        return process(instance);
    }
    bool start = !instance->have_rx || event->at_us - instance->last_rx_us >= instance->gap_us;
    instance->last_rx_us = event->at_us;
    instance->have_rx = true;
    if (instance->damaged && !start) {
        return -EIO;
    }
    instance->damaged = false;
    if (event->size > instance->config->rx_capacity - instance->size) {
        instance->size = 0;
        instance->protocol = 0;
        instance->damaged = true;
        return -ENOBUFS;
    }
    if (!instance->size) {
        instance->at_boundary = start;
    }
    memcpy(instance->rx + instance->size, event->data, event->size);
    for (size_t i = 0; i < event->size; ++i) {
        instance->received[instance->size + i] = event->at_us;
    }
    instance->size += event->size;
    return process(instance);
}

int tr_register(const tr_config_t *config)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!config || !config->port || !config->protocols || !config->protocol_count ||
        !config->rx_capacity || !config->tx_capacity || !config->send_timeout_ms) {
        return -EINVAL;
    }
    if (config->port->receive) {
        return -EALREADY;
    }
    unsigned types = 0;
    uint32_t gap = 1;
    for (size_t i = 0; i < config->protocol_count; ++i) {
        const tr_protocol_t *protocol = &config->protocols[i];
        if ((protocol->type != TR_PROTOCOL_MODBUS && protocol->type != TR_PROTOCOL_CUSTOM) ||
            !protocol->timeout_us || !protocol->max_frame ||
            protocol->max_frame > config->rx_capacity) {
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
                (protocol->max_frame > 256 && !protocol->modbus.route_count) ||
                config->tx_capacity < 256) {
                return -EINVAL;
            }
        } else if (!protocol->custom.check || !protocol->custom.execute) {
            return -EINVAL;
        }
    }
    size_t fixed = sizeof(tr_instance_t);
    if (config->tx_capacity > SIZE_MAX - fixed) {
        return -EOVERFLOW;
    }
    fixed += config->tx_capacity;
    if (config->rx_capacity > (SIZE_MAX - fixed) / (sizeof(uint64_t) + 1)) {
        return -EOVERFLOW;
    }
    tr_instance_t *instance = k_calloc(1, fixed + config->rx_capacity * (sizeof(uint64_t) + 1));
    if (!instance) {
        return -ENOMEM;
    }
    instance->config = config;
    instance->gap_us = gap;
    instance->received = (uint64_t *)(instance + 1);
    instance->rx = (uint8_t *)(instance->received + config->rx_capacity);
    instance->tx = instance->rx + config->rx_capacity;
    for (size_t i = 0; i < config->protocol_count; ++i) {
        if (config->protocols[i].type == TR_PROTOCOL_MODBUS) {
            int err = mb_server_open(config->protocols[i].modbus.unit, &instance->modbus_server);
            if (err) {
                k_free(instance);
                return err;
            }
        }
    }
    int err = vport_bind(config->port, receive, instance);
    if (err) {
        if (instance->modbus_server) {
            mb_server_close(instance->modbus_server);
        }
        k_free(instance);
    }
    return err;
}
