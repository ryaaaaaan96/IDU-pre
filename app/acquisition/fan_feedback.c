#include "acquisition_internal.h"
#include "dp_callbacks/dp_callbacks.h"
#include "protocol/inc/process_table.h"
#include "protocol/inc/signal_keys.h"
#include "status/inc/status.h"
#include <fan/fan.h>
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#define FAN_SLOT_MS 100U
/* 稳定时间需按实板确认，包含在每路 100 ms 时间槽中。 */
#define FAN_MUX_SETTLE_MS 10U
#define FAN_SAMPLE_MAX_AGE_MS 2000U

typedef struct {
    uint8_t mux_address;
    uint8_t pulses_per_revolution;
    uint32_t rated_rpm;
    process_signal_index_t target;
} fan_channel_t;

/* 采集范围与测点映射独立于 PWM 输出范围；额定转速为 0 表示尚不能换算百分比。 */
static const fan_channel_t channels[] = {
    {0, 2, 0, PROCESS_FAN_SPEED_1},
    {1, 2, 0, PROCESS_FAN_SPEED_2},
    {2, 2, 0, PROCESS_FAN_SPEED_3},
    {3, 2, 0, PROCESS_FAN_SPEED_4},
    {4, 2, 0, PROCESS_FAN_SPEED_5},
    {5, 2, 0, PROCESS_FAN_SPEED_6},
    {6, 2, 0, PROCESS_FAN_SPEED_7},
    {7, 2, 0, PROCESS_FAN_SPEED_8},
};
static const struct gpio_dt_spec selectors[] = {
    GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), fan_select_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), fan_select_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), fan_select_gpios, 2),
};
static const fan_input_config_t input_config = {
    .pwm = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), fan_feedback),
    .timeout_ms = FAN_SAMPLE_MAX_AGE_MS,
};
static fan_input_t input_instance;
static atomic_t ready;
static size_t active_channel;
static int64_t slot_deadline;
static bool scanning;
/* 点值只存于 DP；这里只保留有效性和采样时间。 */
static struct k_spinlock validity_lock;
static int64_t sampled_at[ARRAY_SIZE(channels)];
static bool valid[ARRAY_SIZE(channels)];

int fan_feedback_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        if (channels[i].mux_address > 7 || !channels[i].pulses_per_revolution) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (channels[i].target == channels[j].target ||
                channels[i].mux_address == channels[j].mux_address) {
                return -EINVAL;
            }
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(selectors); ++i) {
        if (!gpio_is_ready_dt(&selectors[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&selectors[i], GPIO_OUTPUT_INACTIVE);
        if (err) {
            return err;
        }
    }
    int err = fan_input_init(&input_instance, &input_config);
    if (!err) {
        err = fan_input_pause(&input_instance);
    }
    if (!err) {
        atomic_set(&ready, 1);
    }
    return err;
}

int fan_feedback_acquire(int64_t *next_deadline_ms)
{
    int64_t now = k_uptime_get();
    if (scanning && now < slot_deadline) {
        *next_deadline_ms = slot_deadline;
        return 0;
    }
    int err = fan_input_pause(&input_instance);
    if (err) {
        return err;
    }
    if (scanning) {
        const fan_channel_t *channel = &channels[active_channel];
        uint32_t rpm;
        int result = fan_input_get_speed(&input_instance, channel->pulses_per_revolution, &rpm);
        k_spinlock_key_t key = k_spin_lock(&validity_lock);
        valid[active_channel] = false;
        k_spin_unlock(&validity_lock, key);
        if (!result && channel->rated_rpm) {
            uint64_t feedback = (uint64_t)rpm * 1000U / channel->rated_rpm;
            if (feedback <= UINT16_MAX) {
                value_t value = {.u16 = (uint16_t)feedback};
                err = dp_set_value_by_index(&(dp_index_access_t){.device_id = DEVICE_LOCAL,
                                                .signal_index = channel->target,
                                                .value_type = VALUE_TYPE_U16,
                                                .flags = DP_ACCESS_FLAG_NONE},
                    &value);
                if (err) {
                    return err;
                }
                key = k_spin_lock(&validity_lock);
                sampled_at[active_channel] = k_uptime_get();
                valid[active_channel] = true;
                k_spin_unlock(&validity_lock, key);
            }
        }
        active_channel = (active_channel + 1U) % ARRAY_SIZE(channels);
    } else {
        active_channel = 0;
        slot_deadline = now;
        scanning = true;
    }
    uint8_t address = channels[active_channel].mux_address;
    for (size_t i = 0; i < ARRAY_SIZE(selectors); ++i) {
        err = gpio_pin_set_dt(&selectors[i], !!(address & BIT(i)));
        if (err) {
            return err;
        }
    }
    err = fan_input_resume(&input_instance, FAN_MUX_SETTLE_MS);
    if (err) {
        return err;
    }
    slot_deadline = acquisition_next_deadline(slot_deadline, FAN_SLOT_MS, k_uptime_get());
    *next_deadline_ms = slot_deadline;
    return 0;
}

void fan_feedback_stop(void)
{
    if (atomic_get(&ready)) {
        (void)fan_input_pause(&input_instance);
    }
    atomic_clear(&ready);
}

/* GET 只检查点值是否可用，不测量、不换算、不覆盖 DP。 */
int points_fan_feedback_read(const dp_callback_context_t *context)
{
    if (!(context->events & DP_CALLBACK_EVENT_GET)) {
        return 0;
    }
    if (!atomic_get(&ready) || status_phase_get() != STATUS_RUN) {
        return -ENODATA;
    }
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        if (channels[i].target != context->signal_index) {
            continue;
        }
        k_spinlock_key_t key = k_spin_lock(&validity_lock);
        bool available = valid[i] && k_uptime_get() - sampled_at[i] <= FAN_SAMPLE_MAX_AGE_MS;
        k_spin_unlock(&validity_lock, key);
        return available ? 0 : -ENODATA;
    }
    return -EINVAL;
}
