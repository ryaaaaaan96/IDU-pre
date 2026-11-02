/* 应用采样周期读取；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>
#include <errno.h>
#include <watchdog/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_points, LOG_LEVEL_INF);

/* 重启/出厂命令先回写响应再动作；200 ms 足够当前帧发完，主站不会只看到超时。 */
#define OPERATION_DELAY_MS 200

static void run_factory_reset(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(factory_reset_work, run_factory_reset);

int points_save_parameters(const dp_callback_context_t *context)
{
    if (context->value->u16 != 1) {
        return -EINVAL;
    }
    int err = dp_flush();
    if (err) {
        return err;
    }
    context->value->u16 = 0;
    return 0;
}

static void run_factory_reset(struct k_work *work)
{
    ARG_UNUSED(work);
    int err = dp_factory_reset(DP_DEVICE_ID_ALL, NULL, NULL);
    LOG_ERR("Factory reset failed (%d)", err);
}

int points_operation_command(const dp_callback_context_t *context)
{
    switch (context->value->u16) {
    case 1:
        watchdog_reboot(OPERATION_DELAY_MS);
        break;
    case 2: {
        int err = dp_restore_defaults(context->device_id);
        if (err) {
            return err;
        }
        break;
    }
    case 3:
        if (k_work_reschedule(&factory_reset_work, K_MSEC(OPERATION_DELAY_MS)) < 0) {
            return -EBUSY;
        }
        break;
    default:
        return -EINVAL;
    }
    context->value->u16 = 0;
    return 0;
}

uint16_t points_sample_period_ms(void)
{
    value_t value;
    if (dp_get_para_by_key(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, VALUE_TYPE_U16, &value) < 0) {
        return CONFIG_IDU_SAMPLE_PERIOD_MS;
    }
    return value.u16;
}
