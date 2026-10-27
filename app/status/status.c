#include "status/inc/status.h"
#include "status_led.h"
#include "protocol/inc/protocol.h"
#include <watchdog/watchdog.h>
#ifdef CONFIG_IDU_IAPV3
#include <iap/iap.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_status, LOG_LEVEL_INF);

/* ==================== 整机状态与后台工作声明 ==================== */

static struct {
    struct k_spinlock lock;
    status_phase_t phase;
} status_runtime;

/* ==================== 状态维护与查询 ==================== */

status_phase_t status_phase_get(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    status_phase_t phase = status_runtime.phase;
    k_spin_unlock(&status_runtime.lock, key);
    return phase;
}

void status_set_phase(status_phase_t phase)
{
    bool start = false;
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase == STATUS_STARTING && phase == STATUS_RUN) {
        status_runtime.phase = STATUS_RUN;
        start = true;
    }
    k_spin_unlock(&status_runtime.lock, key);
    if (start) {
        status_led_start();
    }
}

/* ==================== 故障回调与产品处理动作 ==================== */

static int status_watchdog_fault(const struct watchdog_fault *fault)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    status_phase_t current = status_runtime.phase;
    if (current == STATUS_UNCONFIGURED) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EACCES;
    }
    watchdog_action_t action = fault->action;
    if (fault->reason != WATCHDOG_FAULT_REPORTED) {
        /* 显式留驻后，只接受新的硬件故障。 */
        if (current == STATUS_FAULT_LOCKED && fault->reason != WATCHDOG_FAULT_GPIO) {
            k_spin_unlock(&status_runtime.lock, key);
            return 0;
        }
        action = current == STATUS_STARTING && fault->reason != WATCHDOG_FAULT_GPIO ?
                 WATCHDOG_ACTION_HOLD : WATCHDOG_ACTION_RESET;
    }
    if (current == STATUS_STARTING && action == WATCHDOG_ACTION_CONTINUE) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EINVAL;
    }
    if (action != WATCHDOG_ACTION_CONTINUE) {
        status_runtime.phase = STATUS_FAULT_LOCKED;
        watchdog_stop(action == WATCHDOG_ACTION_RESET ? WATCHDOG_STOP_ALL : WATCHDOG_STOP_MONITOR);
    }
    k_spin_unlock(&status_runtime.lock, key);
    /* 故障已由 watchdog 记录；CONTINUE 不解除已锁存状态或业务停止标志。 */
    if (action == WATCHDOG_ACTION_CONTINUE) {
        LOG_ERR("%s: state %d error %d action %d", fault->description, current, fault->error, action);
        return 0;
    }

    /* 关键动作不等待工作队列；RESET 已在锁内停喂，日志也不阻塞。 */
    protocol_stop();
#ifdef CONFIG_IDU_IAPV3
    iap_stop();
#endif
    LOG_ERR("%s: state %d error %d action %d", fault->description, current, fault->error, action);
    if (action == WATCHDOG_ACTION_RESET) { watchdog_reset(); }

    /* RESET 正常不返回；测试复位钩子返回时仍允许更新现场显示。 */
    status_led_refresh();
    return fault->error;
}

/* ==================== 统一初始化 ==================== */

int status_init(void)
{
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase != STATUS_UNCONFIGURED) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EALREADY;
    }
    status_runtime.phase = STATUS_STARTING;
    k_spin_unlock(&status_runtime.lock, key);

    int err = watchdog_init(status_watchdog_fault);
    if (err) {
        return watchdog_report_fault(err, "Watchdog initialization failed", WATCHDOG_ACTION_HOLD);
    }
    err = status_led_init();
    if (err) {
        return watchdog_report_fault(err, "Indicator initialization failed", WATCHDOG_ACTION_HOLD);
    }
    return 0;
}
