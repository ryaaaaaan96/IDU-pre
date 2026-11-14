#include "acquisition.h"
#include "acquisition_internal.h"
#include "status/inc/status.h"
#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <errno.h>

static K_THREAD_STACK_DEFINE(acquisition_stack, CONFIG_IDU_ACQUISITION_STACK_SIZE);
static struct k_thread acquisition_thread;
static bool initialized;

static void run(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    int err = 0;

    /* 线程随模块初始化启动；镜像确认和启动等待结束前不执行采样。 */
    while (status_phase_get() == STATUS_STARTING) {
        k_msleep(10);
    }
    while (status_phase_get() == STATUS_RUN) {
        int64_t next_di, next_analog, next_fan = ACQUISITION_IDLE_DEADLINE;
#ifdef CONFIG_FAN
        err = fan_feedback_acquire(&next_fan);
#endif
        if (!err) {
            err = di_acquire(&next_di);
        }
        if (!err) {
            err = analog_acquire(&next_analog);
        }
        if (err) {
            /* 保留原始采样错误，即使通知返回 0 也不能继续采样。 */
            watchdog_report_fault(err, "Acquisition failed", WATCHDOG_ACTION_RESET, 200);
            break;
        }
        if (status_phase_get() != STATUS_RUN) {
            break;
        }
        const int64_t next = MIN(MIN(next_di, next_analog), next_fan);
        /* 限制等待长度，以便及时响应整机停止；唤醒不代表必须采样。 */
        const int64_t delay = MIN(next - k_uptime_get(), 100);

        if (delay > 0) {
            k_sleep(K_MSEC(delay));
        }
    }
#ifdef CONFIG_FAN
    fan_feedback_stop();
#endif
    analog_stop();
    status_fault_wait(err);
}

int acquisition_init(void)
{
    if (initialized) {
        return -EALREADY;
    }
    const status_phase_t phase = status_phase_get();
    if (phase != STATUS_STARTING && phase != STATUS_RUN) {
        return -EACCES;
    }
    int err = di_init();
    if (!err) {
        err = analog_init();
    }
#ifdef CONFIG_FAN
    if (!err) {
        err = fan_feedback_init();
    }
#endif
    if (err) {
        return err;
    }
    k_thread_create(&acquisition_thread,
        acquisition_stack,
        K_THREAD_STACK_SIZEOF(acquisition_stack),
        run,
        NULL,
        NULL,
        NULL,
        7,
        0,
        K_FOREVER);
    k_thread_name_set(&acquisition_thread, "acquisition");
    initialized = true;
    k_thread_start(&acquisition_thread);
    return 0;
}
