#include "inc/protocol.h"
#include "inc/process_table.h"
#include "inc/signal_keys.h"
#include "rs485_1.h"
#include "status/inc/status.h"
#include <transmit/transmit.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_protocol, LOG_LEVEL_INF);

#define FORWARD_NODE DT_ALIAS(idu_forward)
#if DT_NODE_HAS_PROP(FORWARD_NODE, uart)
#define FORWARD_UART DT_PHANDLE(FORWARD_NODE, uart)
#else
#define FORWARD_UART DT_PARENT(FORWARD_NODE)
#endif
#define FORWARD_BAUD DT_PROP(FORWARD_UART, current_speed)
#define FORWARD_GAP_US (FORWARD_BAUD > 19200 ? 1750 : DIV_ROUND_UP(35000000U, FORWARD_BAUD))
#ifdef CONFIG_IDU_IAPV3
#define FORWARD_FRAME_SIZE CONFIG_MODBUS_BUFFER_SIZE
extern const tr_modbus_route_t protocol_upgrade_routes[];
extern const size_t protocol_upgrade_route_count;
#else
#define FORWARD_FRAME_SIZE 256
#endif
extern const tr_modbus_point_t protocol_forward_points[];
extern const size_t protocol_forward_point_count;

static vport_t forward_port;
static K_THREAD_STACK_DEFINE(protocol_stack, 8192);
static struct k_thread protocol_thread;

/* 启动时读取站号并填充，绑定后不再修改。 */
static tr_protocol_t protocols[] = {
    {.type = TR_PROTOCOL_MODBUS,
        .max_frame = FORWARD_FRAME_SIZE,
        .timeout_us = 500000,
        .modbus = {.gap_us = FORWARD_GAP_US}},
};
static const tr_config_t ports[] = {
    {.port = &forward_port,
        .protocols = protocols,
        .protocol_count = ARRAY_SIZE(protocols),
        .rx_capacity = FORWARD_FRAME_SIZE,
        .tx_capacity = 256,
        .send_timeout_ms = 100},
};

static void run(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    while (true) {
        vport_event_t event;
        int err = rs485_1_receive(&event);
        if (err || status_phase_get() != STATUS_RUN) {
            continue;
        }
        err = vport_input(&forward_port, &event);
        if (err && err != -EIO) {
            LOG_WRN("RS485-1 event failed (%d)", err);
        }
    }
}

int protocol_init(void)
{
    int err = dp_init();
    if (err) {
        return err;
    }
    err = dp_register_device(DEVICE_LOCAL, &protocol_local_table);
    if (err) {
        return err;
    }
    err = rs485_1_init(&forward_port);
    if (err) {
        return err;
    }
    value_t address = {0};
    err = dp_get_para_by_index(DEVICE_LOCAL, PROCESS_SAVED_ADDRESS, VALUE_TYPE_U8, &address);
    if (err) {
        return err;
    }
    value_t current = {.u16 = address.u8};
    err = dp_set_para_by_index(DEVICE_LOCAL, PROCESS_ADDRESS, VALUE_TYPE_U16, &current);
    if (err) {
        return err;
    }
    protocols[0].modbus.unit = address.u8;
    protocols[0].modbus.points = protocol_forward_points;
    protocols[0].modbus.point_count = protocol_forward_point_count;
#ifdef CONFIG_IDU_IAPV3
    protocols[0].modbus.routes = protocol_upgrade_routes;
    protocols[0].modbus.route_count = protocol_upgrade_route_count;
#endif
    for (size_t i = 0; i < ARRAY_SIZE(ports); ++i) {
        err = tr_register(&ports[i]);
        if (err) {
            return err;
        }
    }
    k_thread_create(&protocol_thread,
        protocol_stack,
        K_THREAD_STACK_SIZEOF(protocol_stack),
        run,
        NULL,
        NULL,
        NULL,
        8,
        0,
        K_FOREVER);
    k_thread_name_set(&protocol_thread, "protocol");
    rs485_1_start();
    k_thread_start(&protocol_thread);
    LOG_INF("Modbus unit %u ready on RS485-1", address.u8);
    return 0;
}
