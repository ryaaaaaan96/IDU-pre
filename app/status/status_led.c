#include "status_led.h"
#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

/* ==================== 各状态的循环步骤 ==================== */

typedef struct {
    bool on;
    uint32_t duration_ms; /**< 0 表示保持当前电平，不再调度。 */
} status_led_step_t;

typedef struct {
    const status_led_step_t *steps;
    size_t count;
} status_led_pattern_t;

static const status_led_step_t starting_steps[] = {
    {.on = true, .duration_ms = 100},
    {.on = false, .duration_ms = 100},
    {.on = true, .duration_ms = 500},
    {.on = false, .duration_ms = 500},
};
static const status_led_step_t run_steps[] = {
    {.on = true, .duration_ms = 500},
    {.on = false, .duration_ms = 500},
};
static const status_led_step_t fault_steps[] = {
    {.on = true, .duration_ms = 100},
    {.on = false, .duration_ms = 100},
};
static const status_led_pattern_t patterns[] = {
    [STATUS_LED_STARTING] = {.steps = starting_steps, .count = ARRAY_SIZE(starting_steps)},
    [STATUS_LED_RUN] = {.steps = run_steps, .count = ARRAY_SIZE(run_steps)},
    [STATUS_LED_FAULT] = {.steps = fault_steps, .count = ARRAY_SIZE(fault_steps)},
};
BUILD_ASSERT(ARRAY_SIZE(patterns) == STATUS_LED_STATE_COUNT);

/* ==================== 步骤播放：仅工作队列推进循环 ==================== */

static const struct gpio_dt_spec run_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static atomic_t ready;
static atomic_t requested_state;
/* 初始化结束后，只有工作队列读写当前状态、步骤和已输出电平。 */
static status_led_state_t active_state;
static size_t step_index;
static bool output_on;
static void update(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(status_led, update);

static void update(struct k_work *work)
{
    ARG_UNUSED(work);
    status_led_state_t state = atomic_get(&requested_state);
    if (state != active_state) {
        active_state = state;
        step_index = 0;
    }
    const status_led_pattern_t *pattern = &patterns[state];
    const status_led_step_t *step = &pattern->steps[step_index];
    int err = 0;
    if (step->on != output_on) {
        err = gpio_pin_set_dt(&run_led, step->on);
        if (!err) {
            output_on = step->on;
        }
    }
    if (err) {
        /* 故障图案本身失败时不递归上报，保留原故障。 */
        if (state != STATUS_LED_FAULT) {
            (void)watchdog_report_fault(err,
                "Indicator output failed",
                state == STATUS_LED_STARTING ? WATCHDOG_ACTION_HOLD : WATCHDOG_ACTION_RESET,
                200);
        }
    } else if (step->duration_ms) {
        step_index = (step_index + 1) % pattern->count;
        /* 不覆盖切换状态时已安排的立即刷新；延迟时不跳过步骤。 */
        k_work_schedule(&status_led, K_MSEC(step->duration_ms));
    }
    /* GPIO 操作或故障回调期间可能切换状态，下一次执行从新状态的第一步开始。 */
    if (atomic_get(&requested_state) != state) {
        k_work_reschedule(&status_led, K_NO_WAIT);
    }
}

/* ==================== 初始化与状态选择 ==================== */

int status_led_init(status_led_state_t state)
{
    if ((unsigned int)state >= ARRAY_SIZE(patterns)) {
        return -EINVAL;
    }
    if (atomic_get(&ready)) {
        return -EALREADY;
    }
    if (!gpio_is_ready_dt(&run_led)) {
        return -ENODEV;
    }
    const status_led_step_t *first = &patterns[state].steps[0];
    int err =
        gpio_pin_configure_dt(&run_led, first->on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
    if (err) {
        return err;
    }
    active_state = state;
    step_index = first->duration_ms ? 1 % patterns[state].count : 0;
    output_on = first->on;
    atomic_set(&requested_state, state);
    atomic_set(&ready, 1);
    if (first->duration_ms) {
        k_work_schedule(&status_led, K_MSEC(first->duration_ms));
    }
    return 0;
}

void status_led_set(status_led_state_t state)
{
    if ((unsigned int)state >= ARRAY_SIZE(patterns) || !atomic_get(&ready)) {
        return;
    }
    if (atomic_set(&requested_state, state) != state) {
        k_work_reschedule(&status_led, K_NO_WAIT);
    }
}
