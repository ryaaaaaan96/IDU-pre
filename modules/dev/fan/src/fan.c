#include <fan/fan.h>
#include <errno.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fan, LOG_LEVEL_ERR);

static int apply_pwm(const struct pwm_dt_spec *pwm, uint32_t per_mille, uint32_t hz)
{
    uint32_t period = NSEC_PER_SEC / hz;
    uint32_t pulse = (uint32_t)((uint64_t)period * per_mille / 1000U);
    return pwm_set_dt(pwm, period, pulse);
}

int fan_output_init(fan_output_t *output, const fan_output_config_t *config)
{
    if (!output || !config || !config->frequency_hz || config->frequency_hz > NSEC_PER_SEC) {
        return -EINVAL;
    }
    if (atomic_get(&output->ready)) {
        return -EALREADY;
    }
    output->pwm = config->pwm;
    if (!pwm_is_ready_dt(&output->pwm)) {
        return -ENODEV;
    }
    int err = apply_pwm(&output->pwm, 0, config->frequency_hz);
    if (!err) {
        k_mutex_init(&output->lock);
        output->frequency_hz = config->frequency_hz;
        output->duty_percent = 0;
        output->duty_per_mille = 0;
        atomic_set(&output->ready, 1);
    }
    return err;
}

int fan_output_set_per_mille(fan_output_t *output, uint32_t per_mille, uint32_t hz)
{
    if (!output || per_mille > 1000U || hz > NSEC_PER_SEC) {
        return -EINVAL;
    }
    if (!atomic_get(&output->ready)) {
        return -EAGAIN;
    }
    k_mutex_lock(&output->lock, K_FOREVER);
    if (!hz) {
        hz = output->frequency_hz;
    }
    int err = apply_pwm(&output->pwm, per_mille, hz);
    if (!err) {
        output->frequency_hz = hz;
        output->duty_percent = per_mille / 10U;
        output->duty_per_mille = per_mille;
    }
    k_mutex_unlock(&output->lock);
    return err;
}

int fan_output_set(fan_output_t *output, uint32_t percent, uint32_t hz)
{
    if (percent > 100U) {
        return -EINVAL;
    }
    return fan_output_set_per_mille(output, percent * 10U, hz);
}

/* 一个线程服务全部输入实例；每轮只测一个完整周期，完成后保持停止。 */
#define FAN_INPUT_CHECK_PERIOD_MS 500
#define FAN_INPUT_THREAD_STACK_SIZE 1536
#define FAN_INPUT_THREAD_PRIORITY 8

static K_SEM_DEFINE(input_changed, 0, 1);
static K_MUTEX_DEFINE(inputs_lock);
static fan_input_t *inputs;

static void update_input(fan_input_t *input)
{
    k_spinlock_key_t key = k_spin_lock(&input->lock);
    if (!input->pending) {
        k_spin_unlock(&input->lock, key);
        return;
    }
    uint32_t period = input->pending_period;
    int64_t ticks = input->pending_ticks;
    input->pending = false;
    k_spin_unlock(&input->lock, key);

    input->measuring = false;

    key = k_spin_lock(&input->lock);
    input->period_ticks = period;
    input->sample_ticks = ticks;
    k_spin_unlock(&input->lock, key);
}

static void input_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    k_timeout_t wait = K_FOREVER;
    while (true) {
        (void)k_sem_take(&input_changed, wait);
        int64_t delay = FAN_INPUT_CHECK_PERIOD_MS;
        k_mutex_lock(&inputs_lock, K_FOREVER);
        for (fan_input_t *input = inputs; input; input = input->next) {
            if (input->paused) {
                continue;
            }
            update_input(input);
            int64_t now = k_uptime_get();
            if (now >= input->next_check_ms) {
                int err =
                    input->measuring ? pwm_disable_capture(input->pwm.dev, input->pwm.channel) : 0;
                /* 停止后读取最终快照，避免丢掉超时边界前刚完成的测量。 */
                update_input(input);
                input->next_check_ms = now + FAN_INPUT_CHECK_PERIOD_MS;
                input->measuring = true;
                if (!err) {
                    err = pwm_enable_capture(input->pwm.dev, input->pwm.channel);
                }
                if (err) {
                    input->measuring = false;
                    LOG_ERR("Capture start failed: device=%s channel=%u error=%d",
                        input->pwm.dev->name,
                        input->pwm.channel,
                        err);
                }
            }
            delay = MIN(delay, MAX(INT64_C(1), input->next_check_ms - k_uptime_get()));
        }
        wait = inputs ? K_MSEC(delay) : K_FOREVER;
        k_mutex_unlock(&inputs_lock);
    }
}

K_THREAD_DEFINE(fan_input_worker,
    FAN_INPUT_THREAD_STACK_SIZE,
    input_worker,
    NULL,
    NULL,
    NULL,
    FAN_INPUT_THREAD_PRIORITY,
    0,
    0);

static void captured(const struct device *dev,
    uint32_t channel,
    uint32_t period,
    uint32_t pulse,
    int status,
    void *context)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(channel);
    ARG_UNUSED(pulse);
    fan_input_t *input = context;
    int64_t ticks = k_uptime_ticks();
    k_spinlock_key_t key = k_spin_lock(&input->lock);
    input->pending_period = status ? 0 : period;
    input->pending_ticks = ticks;
    input->pending = true;
    k_spin_unlock(&input->lock, key);
    k_sem_give(&input_changed);
}

int fan_input_init(fan_input_t *input, const fan_input_config_t *config)
{
    if (!input || !config || !config->timeout_ms) {
        return -EINVAL;
    }
    if (atomic_get(&input->ready)) {
        return -EALREADY;
    }
    input->pwm = config->pwm;
    if (!pwm_is_ready_dt(&input->pwm)) {
        return -ENODEV;
    }
    uint64_t rate;
    int err = pwm_get_cycles_per_sec(input->pwm.dev, input->pwm.channel, &rate);
    if (err) {
        return err;
    }
    if (!rate || rate > UINT32_MAX) {
        return -ERANGE;
    }
    input->tick_hz = (uint32_t)rate;
    input->timeout_ms = config->timeout_ms;
    err = pwm_configure_capture(input->pwm.dev,
        input->pwm.channel,
        input->pwm.flags | PWM_CAPTURE_TYPE_PERIOD | PWM_CAPTURE_MODE_SINGLE,
        captured,
        input);
    if (err) {
        return err;
    }
    input->next_check_ms = k_uptime_get() + FAN_INPUT_CHECK_PERIOD_MS;
    input->measuring = true;
    err = pwm_enable_capture(input->pwm.dev, input->pwm.channel);
    if (!err) {
        atomic_set(&input->ready, 1);
        k_mutex_lock(&inputs_lock, K_FOREVER);
        input->next = inputs;
        inputs = input;
        k_mutex_unlock(&inputs_lock);
        /* 处理使能与登记之间已经到达的回调。 */
        k_sem_give(&input_changed);
    } else {
        (void)pwm_configure_capture(input->pwm.dev,
            input->pwm.channel,
            input->pwm.flags | PWM_CAPTURE_TYPE_PERIOD | PWM_CAPTURE_MODE_SINGLE,
            NULL,
            NULL);
    }
    return err;
}

int fan_input_get_speed(fan_input_t *input, uint32_t pulses, uint32_t *rpm)
{
    if (!input || !rpm || !pulses) {
        return -EINVAL;
    }
    if (!atomic_get(&input->ready)) {
        return -EAGAIN;
    }
    k_spinlock_key_t key = k_spin_lock(&input->lock);
    uint32_t period = input->period_ticks;
    uint32_t tick_hz = input->tick_hz;
    int64_t sample_ticks = input->sample_ticks;
    k_spin_unlock(&input->lock, key);
    int64_t age_ms = k_ticks_to_ms_floor64(k_uptime_ticks() - sample_ticks);
    if (!period || age_ms > input->timeout_ms) {
        return -EAGAIN;
    }
    uint64_t result = (uint64_t)tick_hz * 60U / ((uint64_t)period * pulses);
    if (result > UINT32_MAX) {
        return -ERANGE;
    }
    *rpm = (uint32_t)result;
    return 0;
}

int fan_input_pause(fan_input_t *input)
{
    if (!input || !atomic_get(&input->ready)) {
        return -EAGAIN;
    }
    k_mutex_lock(&inputs_lock, K_FOREVER);
    int err = pwm_disable_capture(input->pwm.dev, input->pwm.channel);
    if (!err) {
        input->paused = true;
        input->measuring = false;
        update_input(input);
    }
    k_mutex_unlock(&inputs_lock);
    return err;
}

int fan_input_resume(fan_input_t *input, uint32_t settle_ms)
{
    if (!input || !atomic_get(&input->ready)) {
        return -EAGAIN;
    }
    k_mutex_lock(&inputs_lock, K_FOREVER);
    if (!input->paused) {
        k_mutex_unlock(&inputs_lock);
        return -EALREADY;
    }
    /* 停止期间旧结果仍可读取；恢复时清空，避免外部源切换后串路。 */
    k_spinlock_key_t key = k_spin_lock(&input->lock);
    input->pending = false;
    input->period_ticks = 0;
    input->sample_ticks = 0;
    k_spin_unlock(&input->lock, key);
    input->next_check_ms = k_uptime_get() + settle_ms;
    input->paused = false;
    k_mutex_unlock(&inputs_lock);
    k_sem_give(&input_changed);
    return 0;
}
