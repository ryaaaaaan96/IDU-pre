/* 当前产品只运行本地 process；原通信装配保留于 init/tasks/protocol/ports.c。 */
#include "inc/protocol.h"
#include "tables.h"
#include <dataprocess/process.h>
#include <dataprocess/model.h>

int protocol_register_point_tables(void)
{
    return dp_register_models(&protocol_local_model, 1);
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
