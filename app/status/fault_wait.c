#include "status/inc/status.h"
#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_fault, LOG_LEVEL_INF);

static atomic_t diagnostic_owner;

void status_fault_wait(int error)
{
    /* 启动失败可能同时到达 main 和采集线程，仅保留一个打印者。 */
    if (!atomic_cas(&diagnostic_owner, 0, 1)) {
        while (true) {
            k_sleep(K_FOREVER);
        }
    }
    /* 只查询诊断，不重复上报或恢复业务；RESET 的独立定时器仍可复位。 */
    while (true) {
        struct watchdog_fault fault;
        if (watchdog_fault_get(&fault) == 0) {
            LOG_ERR("Terminal fault: %s, error %d, action %d",
                fault.description,
                fault.error,
                fault.action);
        } else {
            LOG_ERR("Application stopped: error %d", error);
        }
        k_sleep(K_SECONDS(5));
    }
}
