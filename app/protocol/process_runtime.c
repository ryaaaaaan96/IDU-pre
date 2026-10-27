/* 当前产品只运行本地 process；原通信装配保留于 init/tasks/protocol/ports.c。 */
#include "inc/protocol.h"
#include "inc/signal_ids.h"
#include "tables.h"
#include <dataprocess/process.h>
#include <platform_callback.h>

/* 设备归属由应用声明；处理表本身仍常驻 Flash。 */
static DEV_TABLE_S devices[] = {
    {
        .dev_id = DEVICE_LOCAL,
        .port = DP_DEVICE_DEFAULT_PORT,
        .coll_addr = DP_DEVICE_DEFAULT_ADDRESS,
        .dev_code = DP_DEVICE_DEFAULT_CODE,
    },
};

int protocol_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    const struct dp_registered_table *process;
    int err = dp_table_registry_find(TABLE_LOCAL_PROCESS, PRO_DATA_PROCESS, &process);
    if (err) { return err; }
    if (process->count > UINT16_MAX) { return -EOVERFLOW; }
    devices[0].ptl_tbl_cfg[0] = (DEV_TBL_CFG_S){
        .proto_type = PRO_DATA_PROCESS,
        .pst_table = process->rows,
        .sig_num = process->count,
    };
    return dp_register_devices(devices, ARRAY_SIZE(devices));
}

int protocol_init(void)
{
    int err = protocol_register_point_tables();
    if (!err) { err = dp_init(); }
    if (err) { return err; }

    /* 本轮只使用 RAM 默认值；不挂载参数区，避免远端未决意图因设备暂缺被判为不兼容。 */
    static const struct dp_process_plan plan = {
        .initialize_all = true,
        .init_keys = DP_PROCESS_DEFAULT_INIT_KEYS,
        .init_count = DP_PROCESS_DEFAULT_COUNT,
        .timers = DP_PROCESS_DEFAULT_TIMERS,
        .timer_count = DP_PROCESS_DEFAULT_COUNT,
    };
    err = dp_process_init(&plan);
    if (!err) { err = dp_reference_init(); }
    return err;
}
