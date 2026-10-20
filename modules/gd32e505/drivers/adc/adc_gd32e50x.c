/* Synchronous ADC0 backend; policy and sample averaging belong to the caller. */
#define DT_DRV_COMPAT gd_gd32e50x_adc

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <gd32_adc.h>
#include <gd32_rcu.h>

struct gd32_adc_config {
    uint32_t reg;
    const struct device *clock;
    uint16_t clock_id;
    struct reset_dt_spec reset;
    const struct pinctrl_dev_config *pins;
    uint32_t prescaler;
    uint16_t divisor;
    uint16_t vdda_mv;
};

struct gd32_adc_data {
    struct k_mutex lock;
    uint16_t configured;
    uint8_t sample_time[16];
    bool calibrated;
};

/* Integral ticks follow the GD32 driver's rounded-up convention. */
static const uint16_t acquisition_ticks[] = {2, 8, 14, 29, 42, 56, 72, 240};
static const uint8_t sample_times[] = {
    ADC_SAMPLETIME_1POINT5, ADC_SAMPLETIME_7POINT5,
    ADC_SAMPLETIME_13POINT5, ADC_SAMPLETIME_28POINT5,
    ADC_SAMPLETIME_41POINT5, ADC_SAMPLETIME_55POINT5,
    ADC_SAMPLETIME_71POINT5, ADC_SAMPLETIME_239POINT5,
};

static int gd32_adc_wait_clear(uint32_t reg, uint32_t mask)
{
    int64_t deadline = k_uptime_get() + 10;

    while (ADC_CTL1(reg) & mask) {
        if (k_uptime_get() >= deadline) {
            return -ETIMEDOUT;
        }
    }
    return 0;
}

static int gd32_adc_calibrate(const struct device *dev)
{
    const struct gd32_adc_config *cfg = dev->config;
    struct gd32_adc_data *data = dev->data;
    int err;

    data->calibrated = false;
    ADC_CTL1(cfg->reg) |= ADC_CTL1_RSTCLB;
    err = gd32_adc_wait_clear(cfg->reg, ADC_CTL1_RSTCLB);
    if (err) {
        return err;
    }
    ADC_CTL1(cfg->reg) |= ADC_CTL1_CLB;
    err = gd32_adc_wait_clear(cfg->reg, ADC_CTL1_CLB);
    data->calibrated = err == 0;
    return err;
}

/* Also resets an unfinished conversion/calibration before the next request. */
static int gd32_adc_prepare(const struct device *dev)
{
    const struct gd32_adc_config *cfg = dev->config;

    adc_deinit(cfg->reg);
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(cfg->reg, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(cfg->reg, ADC_SCAN_MODE, DISABLE);
    adc_data_alignment_config(cfg->reg, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(cfg->reg, ADC_REGULAR_CHANNEL, 1);
    adc_external_trigger_source_config(cfg->reg, ADC_REGULAR_CHANNEL,
                                       ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(cfg->reg, ADC_REGULAR_CHANNEL, ENABLE);
    adc_enable(cfg->reg);
    k_busy_wait(10);
    return gd32_adc_calibrate(dev);
}

static int gd32_adc_channel_setup(const struct device *dev,
                                 const struct adc_channel_cfg *channel)
{
    struct gd32_adc_data *data = dev->data;
    unsigned int index = 0;

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (channel->channel_id >= ARRAY_SIZE(data->sample_time)) {
        return -EINVAL;
    }
    if (channel->gain != ADC_GAIN_1 || channel->reference != ADC_REF_VDD_1 ||
        channel->differential) {
        return -ENOTSUP;
    }
    if (channel->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
        if (ADC_ACQ_TIME_UNIT(channel->acquisition_time) != ADC_ACQ_TIME_TICKS) {
            return -ENOTSUP;
        }
        for (; index < ARRAY_SIZE(acquisition_ticks); ++index) {
            if (acquisition_ticks[index] == ADC_ACQ_TIME_VALUE(channel->acquisition_time)) {
                break;
            }
        }
        if (index == ARRAY_SIZE(acquisition_ticks)) {
            return -ENOTSUP;
        }
    }
    k_mutex_lock(&data->lock, K_FOREVER);
    data->sample_time[channel->channel_id] = sample_times[index];
    data->configured |= BIT(channel->channel_id);
    k_mutex_unlock(&data->lock);
    return 0;
}

static int gd32_adc_read(const struct device *dev, const struct adc_sequence *seq)
{
    const struct gd32_adc_config *cfg = dev->config;
    struct gd32_adc_data *data = dev->data;
    uint32_t resolution;
    int err = 0;

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!seq->channels || (seq->channels & ~0xffffU)) {
        return -EINVAL;
    }
    if ((seq->channels & (seq->channels - 1U)) || seq->oversampling || seq->options) {
        return -ENOTSUP;
    }
    if (!seq->buffer || (uintptr_t)seq->buffer % sizeof(uint16_t)) {
        return -EINVAL;
    }
    if (seq->buffer_size < sizeof(uint16_t)) {
        return -ENOMEM;
    }
    switch (seq->resolution) {
    case 12: resolution = ADC_RESOLUTION_12B; break;
    case 10: resolution = ADC_RESOLUTION_10B; break;
    case 8: resolution = ADC_RESOLUTION_8B; break;
    case 6: resolution = ADC_RESOLUTION_6B; break;
    default: return -EINVAL;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    if ((data->configured & seq->channels) != seq->channels) {
        err = -EINVAL;
        goto out;
    }
    if (!data->calibrated || seq->calibrate) {
        err = gd32_adc_prepare(dev);
        if (err) {
            goto failed;
        }
    }
    unsigned int channel = find_lsb_set(seq->channels) - 1U;

    adc_resolution_config(cfg->reg, resolution);
    adc_regular_channel_config(cfg->reg, 0, channel, data->sample_time[channel]);
    adc_flag_clear(cfg->reg, ADC_FLAG_EOC);
    adc_software_trigger_enable(cfg->reg, ADC_REGULAR_CHANNEL);
    int64_t deadline = k_uptime_get() + 2;

    while (!adc_flag_get(cfg->reg, ADC_FLAG_EOC)) {
        if (k_uptime_get() >= deadline) {
            err = -ETIMEDOUT;
            goto failed;
        }
    }
    *(uint16_t *)seq->buffer = adc_regular_data_read(cfg->reg);
    goto out;

failed:
    adc_disable(cfg->reg);
    data->calibrated = false;
out:
    k_mutex_unlock(&data->lock);
    return err;
}

#ifdef CONFIG_ADC_ASYNC
static int gd32_adc_read_async(const struct device *dev, const struct adc_sequence *seq,
                               struct k_poll_signal *signal)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(seq);
    ARG_UNUSED(signal);
    return -ENOTSUP;
}
#endif

static DEVICE_API(adc, gd32_adc_api) = {
    .channel_setup = gd32_adc_channel_setup,
    .read = gd32_adc_read,
#ifdef CONFIG_ADC_ASYNC
    .read_async = gd32_adc_read_async,
#endif
};

static int gd32_adc_init(const struct device *dev)
{
    const struct gd32_adc_config *cfg = dev->config;
    struct gd32_adc_data *data = dev->data;
    uint32_t bus_hz;
    int err;

    k_mutex_init(&data->lock);
    if (!device_is_ready(cfg->clock) || !device_is_ready(cfg->reset.dev)) {
        return -ENODEV;
    }
    err = clock_control_get_rate(cfg->clock, (clock_control_subsys_t)&cfg->clock_id,
                                &bus_hz);
    if (err) {
        return err;
    }
    uint32_t max_hz = cfg->vdda_mv < 2400 ? 14000000U : 35000000U;

    if (cfg->vdda_mv < 1710 || cfg->vdda_mv > 3630 ||
        bus_hz < 100000U * cfg->divisor || bus_hz > max_hz * cfg->divisor) {
        return -EINVAL;
    }
    err = pinctrl_apply_state(cfg->pins, PINCTRL_STATE_DEFAULT);
    if (err) {
        return err;
    }
    err = clock_control_on(cfg->clock, (clock_control_subsys_t)&cfg->clock_id);
    if (err) {
        return err;
    }
    err = reset_line_toggle_dt(&cfg->reset);
    if (!err) {
        rcu_adc_clock_config(cfg->prescaler);
        err = gd32_adc_prepare(dev);
        if (err) {
            adc_disable(cfg->reg);
        }
    }
    if (err) {
        clock_control_off(cfg->clock, (clock_control_subsys_t)&cfg->clock_id);
    }
    return err;
}

/* ADC mode and prescaler are shared; don't silently claim multi-instance support. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1);

#define GD32_ADC_DEFINE(n)                                                        \
    BUILD_ASSERT(DT_INST_REG_ADDR(n) == ADC0, "Only ADC0 is supported");            \
    PINCTRL_DT_INST_DEFINE(n);                                                    \
    static struct gd32_adc_data gd32_adc_data_##n;                                \
    static const struct gd32_adc_config gd32_adc_config_##n = {                   \
        .reg = DT_INST_REG_ADDR(n),                                              \
        .clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                            \
        .clock_id = DT_INST_CLOCKS_CELL(n, id),                                   \
        .reset = RESET_DT_SPEC_INST_GET(n),                                      \
        .pins = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                \
        .prescaler = UTIL_CAT(RCU_CKADC_CKAPB2_DIV, DT_INST_PROP(n, clock_prescaler)), \
        .divisor = DT_INST_PROP(n, clock_prescaler),                              \
        .vdda_mv = DT_INST_PROP(n, vdda_millivolts),                               \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(n, gd32_adc_init, NULL, &gd32_adc_data_##n,               \
                          &gd32_adc_config_##n, POST_KERNEL,                    \
                          CONFIG_ADC_INIT_PRIORITY, &gd32_adc_api);

DT_INST_FOREACH_STATUS_OKAY(GD32_ADC_DEFINE)
