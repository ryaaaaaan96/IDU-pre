/* 当前产品只运行本地 process；原通信装配保留于 init/tasks/protocol/ports.c。 */
#include "inc/protocol.h"
#include "inc/signal_ids.h"
#include "tables.h"
#include <dataprocess/process.h>
#include <dataprocess/model.h>

static struct dp_model_cfg models[] = {
    {.device_id = DEVICE_LOCAL},
};

int protocol_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    const struct dp_registered_table *process;
    int err = dp_table_registry_find(TABLE_LOCAL_PROCESS, PRO_DATA_PROCESS, &process);
    if (err) { return err; }
    models[0].points = process->rows;
    models[0].point_count = process->count;
    return dp_register_models(models, ARRAY_SIZE(models));
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
    return err;
}
