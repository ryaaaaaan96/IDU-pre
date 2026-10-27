/* 当前产品只运行本地 process；原通信装配保留于 init/tasks/protocol/ports.c。 */
#include "inc/protocol.h"
#include "inc/signal_ids.h"
#include "tables.h"
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <platform_callback.h>
#include <zephyr/sys/atomic.h>

static const struct data_device_cfg bindings[] = {
    {
        .dev_id = DEVICE_LOCAL,
        .process_table = TABLE_LOCAL_PROCESS,
        .collect_table = DP_DATA_DEFAULT_COLLECT_TABLE,
        .collect_port = DP_DEVICE_DEFAULT_PORT,
        .collect_addr = DP_DEVICE_DEFAULT_ADDRESS,
    },
};
static DEV_TABLE_S devices[ARRAY_SIZE(bindings)];
static struct k_thread samples, business;
K_THREAD_STACK_DEFINE(sample_stack, CONFIG_DP_POINTS_STACK_SIZE);
K_THREAD_STACK_DEFINE(business_stack, CONFIG_DP_PROCESS_STACK_SIZE);
static atomic_t stopped;

const DEV_TABLE_S *protocol_device_table(void) { return devices; }
uint16_t protocol_device_count(void) { return ARRAY_SIZE(devices); }
bool protocol_stopped(void) { return atomic_get(&stopped) != 0; }

/* 保留调用方接口；当前没有通信维护任务。 */
int protocol_error(void) { return 0; }

void protocol_stop(void)
{
    atomic_set(&stopped, 1);
    dp_stop();
}

int protocol_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    int err = dp_data_device_bind(bindings, ARRAY_SIZE(bindings), devices);
    if (err) { return err; }
    pltcb_reg_cfg_get_dev_tbl(protocol_device_table);
    pltcb_reg_cfg_get_dev_num(protocol_device_count);
    return 0;
}

int protocol_init(void)
{
    if (protocol_stopped()) { return -ECANCELED; }
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
    if (err) { return err; }

    /* 装配成功后再启动，模块不隐式创建线程。 */
    k_thread_create(&samples, sample_stack, K_THREAD_STACK_SIZEOF(sample_stack),
                    dp_samples_task, NULL, NULL, NULL, 5, 0, K_FOREVER);
    k_thread_create(&business, business_stack, K_THREAD_STACK_SIZEOF(business_stack),
                    dp_business_task, NULL, NULL, NULL, 6, 0, K_FOREVER);
    if (protocol_stopped()) { return -ECANCELED; }
    k_thread_start(&samples);
    k_thread_start(&business);
    return 0;
}
