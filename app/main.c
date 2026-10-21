#include "acquisition/idu_acquisition.h"
#include "process/idu_process.h"
#include "cfg/inc/device.h"
#include "cfg/inc/ports.h"
#include "status/idu_status.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <gpio/idu_inputs.h>
#include <platform_callback.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(idu_main, LOG_LEVEL_INF);

static int start_points(void)
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
    err = idu_ports_init();
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

    /* 保留降级策略：存储不可用时可采集，但禁止参数写入和试运行确认。 */
    err = dp_params_init(config, storage_id);
    if (err) {
        LOG_ERR("Parameter storage unavailable (%d); writes disabled", err);
    } else {
        err = dp_params_recover();
        if (err) {
            LOG_WRN("Parameter transaction awaiting readback: %d", err);
        }
    }

    /* INIT 和已注册定时任务就绪后才启动轮询。 */
    static const struct dp_process_plan process_plan = {
        .initialize_all = true,
    };

    err = dp_process_start(&process_plan);
    if (!err) {
        err = dp_reference_start();
    }
    if (!err) {
        err = co_start();
    }
    return err;
}

int idu_startup(struct idu_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    if (status->phase != IDU_STATUS_UNCONFIGURED) {
        return -EALREADY;
    }

    int err = idu_status_start(status);

    if (!err) {
        err = idu_inputs_init();
    }
    if (!err) {
        err = start_points();
    }
#ifdef CONFIG_IDU_IAPV3
    if (!err) {
        err = iap_start();
    }
#endif
    if (err) {
        idu_status_fail(status, IDU_STATUS_STARTING, err);
        LOG_ERR("Startup failed: %d; watchdog feeding stopped", err);
        return err;
    }

    idu_status_set_phase(status, IDU_STATUS_WAITING);
    return 0;
}

int main(void)
{
    struct idu_status status = {0};
    int err = idu_startup(&status);

    if (err) {
        return err;
    }

    while (true) {
        int64_t next_sample = k_uptime_get() + idu_sample_period_ms();
        err = idu_acquisition_cycle(&status);
        if (err) {
            return err;
        }
        int64_t delay = next_sample - k_uptime_get();

        if (delay > 0) {
            k_sleep(K_MSEC(delay));
        }
    }
}
