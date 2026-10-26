/** @file
 * @brief status 内部协作接口，不供业务模块调用。
 */
#ifndef STATUS_INTERNAL_H
#define STATUS_INTERNAL_H

#include "status/status.h"
#include <zephyr/kernel.h>

/* 任务期限和整机阶段共用锁，保留超时锁存与停止业务间的并发保护。 */
struct status_runtime {
    struct k_spinlock lock;
    status_phase_t phase;
};
extern struct status_runtime status_runtime;

/* LED：初始化、设置运行起点和更新输出。 */
int status_led_init(void);
void status_led_start(void);
void status_led_update(status_phase_t phase);

/* 外部看门狗：硬件适配与健康检查。 */
int status_watchdog_init(void);
void status_watchdog_stop(void);
int status_watchdog_error(void);
int status_watchdog_check(status_fault_phase_t phase);

/* 任务监控：调用方持有 status_runtime.lock。 */
void status_tasks_stop(void);

#endif
