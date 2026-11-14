#include "acquisition_internal.h"
#include "dp_callbacks/dp_callbacks.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/shell/shell.h>
#include <errno.h>

#define INTERNAL_INPUT_COUNT 12U
#define EXTERNAL_INPUT_COUNT 8U

/* PT1–PT20 为 PT100 前端输出；缺少前端参数时仅保存 ADC 原始值。 */
static const struct adc_dt_spec inputs[] = {
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt1),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt2),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt3),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt4),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt5),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt6),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt7),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt8),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt9),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt10),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt11),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt12),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt13),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt14),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt15),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt16),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt17),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt18),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt19),
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), pt20),
};
BUILD_ASSERT(ARRAY_SIZE(inputs) == INTERNAL_INPUT_COUNT + EXTERNAL_INPUT_COUNT);
static uint16_t samples[ARRAY_SIZE(inputs)];
static struct k_mutex sample_lock;
static bool valid;
static int64_t deadline;

int analog_init(void)
{
    k_mutex_init(&sample_lock);
    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        if (!adc_is_ready_dt(&inputs[i])) {
            return -ENODEV;
        }
        int err = adc_channel_setup_dt(&inputs[i]);
        if (err) {
            return err;
        }
        /* 外部序列按 CH0–CH7 升序返回，对应 PT13–PT20。 */
        if (i >= INTERNAL_INPUT_COUNT &&
            (inputs[i].dev != inputs[INTERNAL_INPUT_COUNT].dev ||
                inputs[i].channel_id != i - INTERNAL_INPUT_COUNT || inputs[i].resolution != 12)) {
            return -EINVAL;
        }
    }
    return 0;
}

void analog_stop(void)
{
    k_mutex_lock(&sample_lock, K_FOREVER);
    valid = false;
    k_mutex_unlock(&sample_lock);
}

int analog_acquire(int64_t *next_deadline_ms)
{
    if (k_uptime_get() >= deadline) {
        uint16_t raw[ARRAY_SIZE(inputs)];
        for (size_t i = 0; i < INTERNAL_INPUT_COUNT; ++i) {
            struct adc_sequence sequence = {.buffer = &raw[i], .buffer_size = sizeof(raw[i])};
            int err = adc_sequence_init_dt(&inputs[i], &sequence);
            if (!err) {
                err = adc_read_dt(&inputs[i], &sequence);
            }
            if (err) {
                analog_stop();
                return err;
            }
        }
        /* 同一芯片一次读取八路，只建立一次流水线，合计十帧。 */
        struct adc_sequence external = {
            .channels = BIT_MASK(EXTERNAL_INPUT_COUNT),
            .buffer = &raw[INTERNAL_INPUT_COUNT],
            .buffer_size = EXTERNAL_INPUT_COUNT * sizeof(raw[0]),
            .resolution = 12,
        };
        int err = adc_read(inputs[INTERNAL_INPUT_COUNT].dev, &external);

        if (err) {
            analog_stop();
            return err;
        }
        k_mutex_lock(&sample_lock, K_FOREVER);
        memcpy(samples, raw, sizeof(samples));
        valid = true;
        k_mutex_unlock(&sample_lock);
        deadline = acquisition_next_deadline(deadline, 500, k_uptime_get());
    }
    *next_deadline_ms = deadline;
    return 0;
}

int points_analog_read(const dp_callback_context_t *context)
{
    /* 温度换算尚未确认，禁止将原始 ADC 或初值作为有效温度返回。 */
    return (context->events & DP_CALLBACK_EVENT_GET) ? -ENODATA : 0;
}

static int cmd_pt_show(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    uint16_t snapshot[ARRAY_SIZE(inputs)];
    bool snapshot_valid;

    k_mutex_lock(&sample_lock, K_FOREVER);
    memcpy(snapshot, samples, sizeof(snapshot));
    snapshot_valid = valid;
    k_mutex_unlock(&sample_lock);
    /* 串口输出使用快照，避免持锁期间阻塞采集线程和风扇轮询。 */
    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        shell_print(shell, "PT%u valid=%u raw=%u", (unsigned)i + 1U, snapshot_valid, snapshot[i]);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(pt_commands,
    SHELL_CMD_ARG(show, NULL, "Show PT100 frontend raw ADC samples.", cmd_pt_show, 1, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(pt, &pt_commands, "PT100 diagnostics.", NULL);
