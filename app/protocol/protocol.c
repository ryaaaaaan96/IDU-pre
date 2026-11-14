#include "inc/protocol.h"
#include "inc/process_table.h"
#include "inc/signal_keys.h"
#include "status/inc/status.h"
#include <transmit/transmit.h>
#include <errno.h>

#ifdef CONFIG_IDU_IAPV3
#define FORWARD_FRAME_SIZE CONFIG_MODBUS_BUFFER_SIZE
extern const tr_modbus_route_t protocol_upgrade_routes[];
extern const size_t protocol_upgrade_route_count;
#else
#define FORWARD_FRAME_SIZE 256
#endif
extern const tr_modbus_point_t protocol_forward_points[];
extern const size_t protocol_forward_point_count;
static tr_engine_t *engine;
static tr_session_t **sessions;
static size_t session_count;
static tr_protocol_t protocols[] = {
    {.type = TR_PROTOCOL_MODBUS, .max_frame = FORWARD_FRAME_SIZE, .timeout_us = 500000},
};

static bool accept_event(void *context, const vport_event_t *event)
{
    ARG_UNUSED(context);
    ARG_UNUSED(event);
    return status_phase_get() == STATUS_RUN;
}

uint8_t protocol_unit(void)
{
    return protocols[0].modbus.unit;
}

void protocol_deinit(void)
{
    while (session_count) {
        (void)tr_session_destroy(sessions[--session_count]);
    }
    k_free(sessions);
    sessions = NULL;
    if (engine) {
        (void)tr_engine_destroy(engine);
        engine = NULL;
    }
}

static int init_data(void)
{
    int err = dp_init();
    if (err) {
        return err;
    }
    err = dp_register_device(DEVICE_LOCAL, &protocol_local_table);
    if (err) {
        return err;
    }
    return 0;
}

static int configure_protocol(void)
{
    value_t address = {0};
    int err = dp_get_value_by_index(&(dp_index_access_t){.device_id = DEVICE_LOCAL,
                                        .signal_index = PROCESS_UNIT_ID,
                                        .value_type = VALUE_TYPE_U16,
                                        .flags = DP_ACCESS_FLAG_NONE},
        &address);
    if (err) {
        return err;
    }
    protocols[0].modbus.unit = (uint8_t)address.u16;
    protocols[0].modbus.points = protocol_forward_points;
    protocols[0].modbus.point_count = protocol_forward_point_count;
#ifdef CONFIG_IDU_IAPV3
    protocols[0].modbus.routes = protocol_upgrade_routes;
    protocols[0].modbus.route_count = protocol_upgrade_route_count;
#endif
    return 0;
}

int protocol_init(
    vport_t *port, const protocol_channel_config_t *configs, size_t count, uint32_t gap_us)
{
    if (!port || !configs || !count || !gap_us) {
        return -EINVAL;
    }
    if (engine || sessions) {
        return -EALREADY;
    }
    int err = init_data();
    if (!err) {
        err = configure_protocol();
    }
    if (err) {
        return err;
    }
    protocols[0].modbus.gap_us = gap_us;
    tr_engine_config_t config = {.port = port,
        .protocols = protocols,
        .protocol_count = ARRAY_SIZE(protocols),
        .send_timeout_ms = 100};
    err = tr_engine_create(&config, &engine);
    if (err) {
        return err;
    }
    sessions = k_calloc(count, sizeof(*sessions));
    if (!sessions) {
        protocol_deinit();
        return -ENOMEM;
    }
    for (size_t i = 0; i < count; ++i) {
        tr_session_config_t session = {
            .channel_id = configs[i].channel_id,
            .rx_capacity = configs[i].upgrade ? FORWARD_FRAME_SIZE : 256,
            .tx_capacity = 256,
            .max_frame = configs[i].upgrade ? FORWARD_FRAME_SIZE : 256,
            .allow_routes = configs[i].upgrade,
            .accept = accept_event,
        };
        err = tr_session_create(engine, &session, &sessions[i]);
        if (err) {
            protocol_deinit();
            return err;
        }
        ++session_count;
    }
    return 0;
}
