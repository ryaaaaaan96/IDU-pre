/** @file
 * @brief status 与任务监控之间的内部协作接口，不供业务包含。
 */
#ifndef STATUS_TASK_MONITOR_H
#define STATUS_TASK_MONITOR_H

#include "status/inc/status.h"
#include <zephyr/kernel.h>

/* 任务期限与故障处理共用状态锁，确保故障锁存后不能再次登记或续期。 */
struct status_runtime {
    struct k_spinlock lock;
    status_phase_t phase;
};
extern struct status_runtime status_runtime;

/** @brief 停止期限定时器，保留任务及诊断记录；调用方须持有状态锁。 */
void task_monitor_stop(void);
/** @brief 查询 status 持有的外部看门狗，不改变喂狗状态。 */
int status_watchdog_error(void);

#endif
