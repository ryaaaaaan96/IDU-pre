#include <zephyr/ztest.h>
/* 通过 adc_*() 测试真实后端，仅替换硬件。 */
#include "../../../modules/gd32e505/drivers/adc/adc_gd32e50x.c"

int test_pin_error;
static uint32_t ctl1, calibration_stall, bus_rate;
static bool conversion_stall, enabled;
static unsigned int conversions, resets, selected_channel, selected_time, selected_resolution;
static int clock_error, reset_error, clock_off_count;
static bool hold_conversion;
K_SEM_DEFINE(conversion_started, 0, 1);
K_SEM_DEFINE(conversion_release, 0, 1);

uint32_t *test_ctl1(void)
{
    ctl1 &= calibration_stall;
    if (ctl1) {
        k_busy_wait(1000); /* 外设卡住时推进 native_sim 时间。 */
    }
    return &ctl1;
}

void adc_deinit(uint32_t reg)
{
    ctl1 = 0;
    resets++;
}

void adc_mode_config(uint32_t mode)
{
}

void adc_special_function_config(uint32_t reg, uint32_t function, uint32_t enable)
{
}

void adc_data_alignment_config(uint32_t reg, uint32_t align)
{
}

void adc_channel_length_config(uint32_t reg, uint32_t group, uint32_t length)
{
}

void adc_external_trigger_source_config(uint32_t reg, uint32_t group, uint32_t source)
{
}

void adc_external_trigger_config(uint32_t reg, uint32_t group, uint32_t enable)
{
}

void adc_enable(uint32_t reg)
{
    enabled = true;
}

void adc_disable(uint32_t reg)
{
    enabled = false;
}

void adc_resolution_config(uint32_t reg, uint32_t resolution)
{
    selected_resolution = resolution;
}

void adc_regular_channel_config(uint32_t reg, uint32_t rank, uint32_t channel, uint32_t sample_time)
{
    selected_channel = channel;
    selected_time = sample_time;
}

void adc_flag_clear(uint32_t reg, uint32_t flag)
{
}

void adc_software_trigger_enable(uint32_t reg, uint32_t group)
{
    conversions++;
}

int adc_flag_get(uint32_t reg, uint32_t flag)
{
    if (hold_conversion) {
        hold_conversion = false;
        k_sem_give(&conversion_started);
        k_sem_take(&conversion_release, K_FOREVER);
    }
    if (conversion_stall) {
        k_busy_wait(1000);
    }
    return !conversion_stall;
}

uint16_t adc_regular_data_read(uint32_t reg)
{
    return 100 + selected_channel;
}

void rcu_adc_clock_config(uint32_t prescaler)
{
}

static int clock_on(const struct device *dev, clock_control_subsys_t sys)
{
    return clock_error;
}

static int clock_off(const struct device *dev, clock_control_subsys_t sys)
{
    clock_off_count++;
    return 0;
}

static int clock_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
    *rate = bus_rate;
    return 0;
}

static DEVICE_API(clock_control, clock_api) = {
    .on = clock_on,
    .off = clock_off,
    .get_rate = clock_rate,
};

static int reset_toggle(const struct device *dev, uint32_t id)
{
    return reset_error;
}

static DEVICE_API(reset, reset_api) = {.line_toggle = reset_toggle};
DEVICE_DEFINE(adc_test_clock, "adc_test_clock", NULL, NULL, NULL, NULL, POST_KERNEL, 1, &clock_api);
DEVICE_DEFINE(adc_test_reset, "adc_test_reset", NULL, NULL, NULL, NULL, POST_KERNEL, 1, &reset_api);
static struct gd32_adc_data test_data;
static struct gd32_adc_config test_config = {
    .reg = ADC0,
    .clock = DEVICE_GET(adc_test_clock),
    .reset = {.dev = DEVICE_GET(adc_test_reset)},
    .divisor = 6,
    .vdda_mv = 3300,
};
DEVICE_DEFINE(adc_under_test,
    "adc_under_test",
    NULL,
    NULL,
    &test_data,
    &test_config,
    POST_KERNEL,
    2,
    &gd32_adc_api);
static const struct device *const adc = DEVICE_GET(adc_under_test);
static const struct adc_channel_cfg channel = {
    .channel_id = 13,
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_VDD_1,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 72),
};
static uint16_t buffer[2];
static struct adc_sequence sequence;

static void before(void *fixture)
{
    memset(&test_data, 0, sizeof(test_data));
    ctl1 = calibration_stall = conversions = resets = 0;
    conversion_stall = false;
    clock_error = reset_error = test_pin_error = clock_off_count = 0;
    bus_rate = 180000000;
    test_config.vdda_mv = 3300;
    test_config.divisor = 6;
    buffer[0] = buffer[1] = 0xcafe;
    sequence = (struct adc_sequence){
        .channels = BIT(13),
        .buffer = buffer,
        .buffer_size = sizeof(uint16_t),
        .resolution = 12,
    };
    zassert_ok(gd32_adc_init(adc));
    zassert_ok(adc_channel_setup(adc, &channel));
}

ZTEST(adc_driver, test_single_channel_and_buffer_boundary)
{
    zassert_ok(adc_read(adc, &sequence));
    zassert_equal(buffer[0], 113);
    zassert_equal(buffer[1], 0xcafe);
    zassert_equal(selected_channel, 13);
    zassert_equal(selected_time, ADC_SAMPLETIME_71POINT5);
    sequence.resolution = 10;
    zassert_ok(adc_read(adc, &sequence));
    zassert_equal(selected_resolution, ADC_RESOLUTION_10B);
}

ZTEST(adc_driver, test_reject_setup_without_affecting_existing_channel)
{
    struct adc_channel_cfg cfg = channel;
    cfg.channel_id = 16;
    zassert_equal(adc_channel_setup(adc, &cfg), -EINVAL);
    cfg = channel;
    cfg.gain = ADC_GAIN_2;
    zassert_equal(adc_channel_setup(adc, &cfg), -ENOTSUP);
    cfg = channel;
    cfg.reference = ADC_REF_INTERNAL;
    zassert_equal(adc_channel_setup(adc, &cfg), -ENOTSUP);
    cfg = channel;
    cfg.differential = true;
    zassert_equal(adc_channel_setup(adc, &cfg), -ENOTSUP);
    cfg = channel;
    cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 241);
    zassert_equal(adc_channel_setup(adc, &cfg), -ENOTSUP);
    cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 2);
    zassert_equal(adc_channel_setup(adc, &cfg), -ENOTSUP);
    zassert_ok(adc_read(adc, &sequence));
    zassert_equal(selected_time, ADC_SAMPLETIME_71POINT5);
}

ZTEST(adc_driver, test_reject_sequences_before_hardware_or_buffer_writes)
{
    sequence.channels = 0;
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    sequence.channels = BIT(16);
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    sequence.channels = BIT(13) | BIT(0);
    zassert_equal(adc_read(adc, &sequence), -ENOTSUP);
    sequence.channels = BIT(0); /* 有效物理通道，但未配置。 */
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    sequence.channels = BIT(13);
    sequence.buffer_size = 1;
    zassert_equal(adc_read(adc, &sequence), -ENOMEM);
    sequence.buffer_size = 2;
    sequence.resolution = 11;
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    sequence.resolution = 12;
    sequence.oversampling = 1;
    zassert_equal(adc_read(adc, &sequence), -ENOTSUP);
    sequence.oversampling = 0;
    sequence.options = &(struct adc_sequence_options){0};
    zassert_equal(adc_read(adc, &sequence), -ENOTSUP);
    sequence.options = NULL;
    sequence.buffer = NULL;
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    sequence.buffer = (uint8_t *)buffer + 1;
    zassert_equal(adc_read(adc, &sequence), -EINVAL);
    zassert_equal(conversions, 0);
    zassert_equal(buffer[0], 0xcafe);
    zassert_equal(buffer[1], 0xcafe);
}

ZTEST(adc_driver, test_conversion_timeout_recovers_without_stale_sample)
{
    conversion_stall = true;
    zassert_equal(adc_read(adc, &sequence), -ETIMEDOUT);
    zassert_equal(buffer[0], 0xcafe);
    zassert_false(enabled);
    zassert_false(test_data.calibrated);
    unsigned int previous_resets = resets;
    conversion_stall = false;
    zassert_ok(adc_read(adc, &sequence));
    zassert_true(resets > previous_resets);
    zassert_equal(buffer[0], 113);
}

ZTEST(adc_driver, test_calibration_timeouts_release_lock_and_recover)
{
    for (unsigned int stage = 0; stage < 2; ++stage) {
        calibration_stall = stage ? ADC_CTL1_CLB : ADC_CTL1_RSTCLB;
        sequence.calibrate = true;
        buffer[0] = 0xcafe;
        zassert_equal(adc_read(adc, &sequence), -ETIMEDOUT);
        zassert_equal(buffer[0], 0xcafe);
        zassert_false(enabled);
        calibration_stall = 0;
        sequence.calibrate = false;
        zassert_ok(adc_read(adc, &sequence));
        zassert_equal(buffer[0], 113);
    }
}

ZTEST(adc_driver, test_init_clock_limits_and_error_propagation)
{
    test_config.vdda_mv = 1800;
    zassert_equal(gd32_adc_init(adc), -EINVAL); /* 30 MHz 超过低电压下的限制。 */
    test_config.vdda_mv = 3300;
    test_config.divisor = 4;
    zassert_equal(gd32_adc_init(adc), -EINVAL); /* 45 MHz 超过 35 MHz。 */
    test_config.divisor = 6;
    test_pin_error = -EIO;
    zassert_equal(gd32_adc_init(adc), -EIO);
    test_pin_error = 0;
    clock_error = -EACCES;
    zassert_equal(gd32_adc_init(adc), -EACCES);
    clock_error = 0;
    reset_error = -EIO;
    zassert_equal(gd32_adc_init(adc), -EIO);
    zassert_equal(clock_off_count, 1);
    reset_error = 0;
    calibration_stall = ADC_CTL1_CLB;
    zassert_equal(gd32_adc_init(adc), -ETIMEDOUT);
    zassert_equal(clock_off_count, 2);
    zassert_false(enabled);
}

ZTEST(adc_driver, test_async_explicitly_unsupported)
{
    zassert_equal(adc_read_async(adc, &sequence, NULL), -ENOTSUP);
    zassert_equal(conversions, 0);
}

struct reader_result {
    uint32_t channel;
    uint16_t sample;
    int error;
};

K_THREAD_STACK_ARRAY_DEFINE(reader_stacks, 2, 2048);
static struct k_thread readers[2];

static void read_thread(void *arg, void *unused1, void *unused2)
{
    struct reader_result *result = arg;
    struct adc_sequence seq = {
        .channels = BIT(result->channel),
        .resolution = 12,
        .buffer = &result->sample,
        .buffer_size = sizeof(result->sample),
    };
    result->error = adc_read(adc, &seq);
}

ZTEST(adc_driver, test_concurrent_readers_cannot_change_inflight_channel)
{
    struct adc_channel_cfg cfg = channel;
    cfg.channel_id = 0;
    zassert_ok(adc_channel_setup(adc, &cfg));
    struct reader_result results[2] = {{.channel = 13}, {.channel = 0}};

    hold_conversion = true;
    k_thread_create(&readers[0],
        reader_stacks[0],
        K_THREAD_STACK_SIZEOF(reader_stacks[0]),
        read_thread,
        &results[0],
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_sem_take(&conversion_started, K_SECONDS(1)));
    k_thread_create(&readers[1],
        reader_stacks[1],
        K_THREAD_STACK_SIZEOF(reader_stacks[1]),
        read_thread,
        &results[1],
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    k_sleep(K_MSEC(1));
    zassert_equal(conversions, 1);
    zassert_equal(selected_channel, 13);
    k_sem_give(&conversion_release);
    for (unsigned int i = 0; i < 2; ++i) {
        zassert_ok(k_thread_join(&readers[i], K_SECONDS(1)));
        zassert_ok(results[i].error);
    }
    zassert_equal(results[0].sample, 113);
    zassert_equal(results[1].sample, 100);
}

ZTEST_SUITE(adc_driver, NULL, NULL, before, NULL, NULL);
