/* 当前产品只运行本地 process；原通信装配保留于 init/tasks/protocol/ports.c。 */
#include "inc/protocol.h"
#include "inc/signal_ids.h"
#include "tables.h"
#include <dataprocess/process.h>
#include <dataprocess/model.h>

/* 产品只声明数据归属和处理表；通信配置不进入 DP 模型。 */
struct data_device_cfg {
    uint16_t device_id;
    uint16_t process_table;
};

static const struct data_device_cfg devices[] = {
    {.device_id = DEVICE_LOCAL, .process_table = TABLE_LOCAL_PROCESS},
};
static struct dp_model_cfg models[ARRAY_SIZE(devices)];

int protocol_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    for (size_t i = 0; i < ARRAY_SIZE(devices); ++i) {
        const struct dp_registered_table *process;
        int err = dp_table_registry_find(devices[i].process_table,
                                        PRO_DATA_PROCESS, &process);
        if (err) { return err; }
        models[i] = (struct dp_model_cfg){
            .device_id = devices[i].device_id,
            .points = process->rows,
            .point_count = process->count,
        };
    }
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
    return dp_process_init(&plan);
}
