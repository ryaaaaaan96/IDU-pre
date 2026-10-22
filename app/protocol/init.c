#include "inc/protocol.h"
#include "tasks.h"
#include <collect/collect.h>
#include <dataprocess/acquisition.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <collect/port.h>
#include <platform_callback.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(idu_protocol, LOG_LEVEL_INF);

static int connect_point_services(void)
{
    static const struct co_data_callbacks callbacks = {
        .sample = dp_accept_samples,
        .initialize = dp_initialize_acquired_device,
    };
    int err = co_set_data_callbacks(&callbacks);
#if defined(CONFIG_DP_PARAMS)
    if (!err) { err = dp_params_set_transport(co_parameter_transport()); }
#endif
    if (!err) { err = dp_para_reg_set_func(co_dp_fast_set); }
    if (!err) { err = idu_dp_tasks_prepare(); }
    if (!err) { err = idu_co_tasks_prepare(); }
    return err;
}

int idu_protocol_init(void)
{
    int err = idu_register_point_tables();

    if (!err) {
        err = dp_init();
    }
    if (err) {
        return err;
    }

    const struct dp_equip_cfg *config = dp_config();

    /* 参数恢复可能读取下游设备，必须先注册所有采集总线。 */
    err = connect_point_services();
    if (!err) { err = idu_ports_init(); }
    if (err) {
        return err;
    }
    int iface = co_port_open(&idu_collect_port);

    if (iface < 0) {
        return iface;
    }
    err = co_init_bus(config, &idu_collect_bus);
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

    err = dp_process_start(&process_plan);
    if (!err) {
        err = dp_reference_start();
    }
    if (!err) {
        err = co_start();
    }
    if (!err) { err = idu_start_forwarding(); }
    if (!err) {
        idu_dp_tasks_start();
        idu_co_tasks_start();
    }
    return err;
}
