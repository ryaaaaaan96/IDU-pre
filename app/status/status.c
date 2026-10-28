#include "status/inc/status.h"
#include "status_led.h"
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

/* 显示策略归 status；LED 播放器不查询整机阶段。 */
static void status_led_sync(void)
{
    static const status_led_state_t states[] = {
        [STATUS_STARTING] = STATUS_LED_STARTING,
        [STATUS_RUN] = STATUS_LED_RUN,
        [STATUS_FAULT_LOCKED] = STATUS_LED_FAULT,
    };
    BUILD_ASSERT(ARRAY_SIZE(states) == STATUS_PHASE_COUNT);
    k_spinlock_key_t key = k_spin_lock(&status_runtime.lock);
    if (status_runtime.phase != STATUS_UNCONFIGURED) {
        status_led_set(states[status_runtime.phase]);
    }
    k_spin_unlock(&status_runtime.lock, key);
}

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
        status_led_sync();
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
    if (current == STATUS_STARTING && fault->action == WATCHDOG_ACTION_CONTINUE) {
        k_spin_unlock(&status_runtime.lock, key);
        return -EINVAL;
    }
    if (fault->action != WATCHDOG_ACTION_CONTINUE) {
        status_runtime.phase = STATUS_FAULT_LOCKED;
    }
    k_spin_unlock(&status_runtime.lock, key);
    /* 故障已由 watchdog 记录；CONTINUE 不解除已锁存状态。 */
    if (fault->action == WATCHDOG_ACTION_CONTINUE) {
        LOG_ERR("%s: state %d error %d action %d",
            fault->description,
            current,
            fault->error,
            fault->action);
        return 0;
    }

    /* 锁存业务阶段并提交日志；回调返回后由 watchdog 执行最终动作。 */
#ifdef CONFIG_IDU_IAPV3
    iap_stop();
#endif
    LOG_ERR("%s: state %d error %d action %d",
        fault->description,
        current,
        fault->error,
        fault->action);

    /* 等待复位或留驻期间，提交故障图案刷新。 */
    status_led_sync();
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
        return watchdog_report_fault(
            err, "Watchdog initialization failed", WATCHDOG_ACTION_HOLD, 0);
    }
    err = status_led_init(STATUS_LED_STARTING);
    if (err) {
        return watchdog_report_fault(
            err, "Indicator initialization failed", WATCHDOG_ACTION_HOLD, 0);
    }
    status_led_sync();
    return 0;
}
