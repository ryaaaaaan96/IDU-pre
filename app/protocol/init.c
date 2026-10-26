#include "inc/protocol.h"
#include "tasks.h"
#include "status/status.h"
#include <collect/collect.h>
#include <dataprocess/acquisition.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <collect/port.h>
#include <platform_callback.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(app_protocol, LOG_LEVEL_INF);

static co_device_online_t reported_states[CONFIG_DP_MAX_DEVICES];

static const char *device_state_name(co_device_online_t state)
{
    switch (state) {
    case CO_DEVICE_OFFLINE: return "offline";
    case CO_DEVICE_ONLINE: return "online";
    case CO_DEVICE_EXCEPTION: return "exception";
    case CO_DEVICE_INITIALIZING: return "initializing";
    default: return "unknown";
    }
}

static void device_state_changed(const struct zbus_channel *channel)
{
    const struct co_device_status *status = zbus_chan_const_msg(channel);
    const struct dp_equip_cfg *config = dp_config();
    if (!config) { return; }
    /* 通道串行调用监听器。计数器和重复错误仍可查询，
     * 仅设备状态转换时输出一条日志。 */
    for (size_t i = 0; i < config->device_count; ++i) {
        if (config->devices[i].dev_id != status->dev_id) { continue; }
        if (reported_states[i] != status->state) {
            LOG_INF("Device %u: %s -> %s (error %d)", status->dev_id,
                    device_state_name(reported_states[i]),
                    device_state_name(status->state), status->error);
            reported_states[i] = status->state;
        }
        break;
    }
}

ZBUS_LISTENER_DEFINE(protocol_device_log, device_state_changed);
ZBUS_CHAN_ADD_OBS(co_device_events, protocol_device_log, 0);

static int connect_point_services(void)
{
    static const struct co_data_callbacks callbacks = {
        .sample = dp_accept_samples,
        .initialize = dp_initialize_acquired_device,
        .offline = dp_invalidate_acquired_device,
    };
    int err = co_set_data_callbacks(&callbacks);
#if defined(CONFIG_DP_PARAMS)
    if (!err) { err = dp_params_set_transport(co_parameter_transport()); }
#endif
    if (!err) { err = dp_para_reg_set_func(co_dp_fast_set); }
    if (!err) { err = protocol_dp_tasks_init(); }
    if (!err) { err = protocol_co_tasks_init(); }
    return err;
}

int protocol_init(void)
{
    if (protocol_stopped()) { return -ECANCELED; }
    int err = protocol_register_point_tables();

    if (!err) {
        err = dp_init();
    }
    if (err) {
        return err;
    }

    const struct dp_equip_cfg *config = dp_config();

    /* 当前采集总线和北向入口共用一条 RS485，冲突必须在开放端口前拒绝。 */
    for (size_t i = 0; i < config->device_count; ++i) {
        if (config->devices[i].bus_id == protocol_collect_bus.id &&
            config->devices[i].unit_id == CONFIG_IDU_FORWARD_UNIT) { return -EADDRINUSE; }
    }

    /* 参数恢复可能读取下游设备，必须先注册所有采集总线。 */
    err = connect_point_services();
    if (!err) { err = protocol_ports_init(); }
    if (err) {
        return err;
    }
    int iface = co_port_open(&protocol_collect_port);

    if (iface < 0) {
        return iface;
    }
    err = co_init_bus(config, &protocol_collect_bus);
    static const struct co_device_policy policy = {
        .retries = CONFIG_IDU_COLLECT_RETRIES,
        .exception_ms = CONFIG_IDU_COLLECT_EXCEPTION_MS,
        .probe_ms = CONFIG_IDU_COLLECT_PROBE_MS,
    };
    for (size_t i = 0; !err && i < config->device_count; ++i) {
        err = co_set_device_policy(config->devices[i].dev_id, &policy);
    }
    if (err) {
        return err;
    }
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));

    /* 保留降级策略：存储不可用时可采集，但禁止持久参数写入和试运行确认。 */
    err = dp_params_init(config, storage_id);
    if (err) {
        LOG_ERR("Parameter storage unavailable (%d); persistent writes disabled", err);
    } else {
        err = dp_params_recover();
        if (err) {
            LOG_WRN("Parameter transaction awaiting readback: %d", err);
        }
    }

    /* INIT 和已注册定时任务就绪后才启动轮询。 */
    static const struct dp_process_plan process_plan = {
        .initialize_all = true,
        .init_keys = DP_PROCESS_DEFAULT_INIT_KEYS,
        .init_count = DP_PROCESS_DEFAULT_COUNT,
        .timers = DP_PROCESS_DEFAULT_TIMERS,
        .timer_count = DP_PROCESS_DEFAULT_COUNT,
    };

    err = dp_process_init(&process_plan);
    if (!err) {
        err = dp_reference_init();
    }
    if (!err) {
        err = co_polling_init();
    }
    if (!err) { err = protocol_forwarding_init(); }
    if (!err) {
        protocol_dp_tasks_start();
        protocol_co_tasks_start();
    }
    return err;
}

void protocol_update_status(void)
{
    uint16_t offline = 0, initializing = 0;
    const struct dp_equip_cfg *config = dp_config();
    if (config) {
        for (size_t i = 0; i < config->device_count; ++i) {
            struct co_device_status remote;
            if (co_device_status(config->devices[i].dev_id, &remote)) { continue; }
            offline += remote.state == CO_DEVICE_OFFLINE;
            initializing += remote.state == CO_DEVICE_INITIALIZING;
        }
    }
    struct dp_param_status parameters;
    dp_params_get_status(&parameters);
    status_update_degradation(offline, initializing, parameters.storage_error);
}
