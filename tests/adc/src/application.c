#include <zephyr/ztest.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <acquisition/acquisition.h>
#include <analog_io.h>

#define TEST_ADC_SPEC(node, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node, idx),
static const struct adc_dt_spec test_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_NODELABEL(idu_io), io_channels, TEST_ADC_SPEC)
};
static const struct analog_adc_config test_group = {
    .channels = test_channels, .channel_count = ARRAY_SIZE(test_channels), .average_count = 10,
};

static const struct device *const emul = DEVICE_DT_GET(DT_NODELABEL(adc0));
static unsigned int samples[16];
static int fail_channel;
static int sample_value(const struct device *dev, unsigned int channel,
                        void *context, uint32_t *result)
{
    if ((int)channel == fail_channel) { return -EIO; }
    *result = channel * 100 + samples[channel]++;
    return 0;
}
static void before(void *fixture)
{
    memset(samples, 0, sizeof(samples));
    fail_channel = -1;
    for (unsigned int channel = 0; channel < 16; ++channel) {
        zassert_ok(adc_emul_raw_value_func_set(emul, channel, sample_value, NULL));
    }
    zassert_ok(acquisition_inputs_init());
}
ZTEST(adc_application, test_original_order_and_ten_sample_integer_average)
{
    const uint8_t order[] = {13, 0, 1, 2, 3, 4, 11, 12};
    struct acquisition_sample sample;
    zassert_ok(acquisition_inputs_sample(&sample));
    zassert_true(sample.valid);
    for (unsigned int i = 0; i < ARRAY_SIZE(order); ++i) {
        zassert_equal(samples[order[i]], 10);
        zassert_equal(sample.raw[i], order[i] * 100 + 4);
    }
    zassert_equal(samples[5], 0);
}
ZTEST(adc_application, test_sampling_error_is_propagated_and_retry_works)
{
    struct acquisition_sample sample;
    fail_channel = 2;
    zassert_equal(acquisition_inputs_sample(&sample), -EIO);
    zassert_false(sample.valid);
    zassert_equal(samples[3], 0);
    fail_channel = -1;
    zassert_ok(acquisition_inputs_sample(&sample));
    zassert_true(sample.valid);
}

ZTEST(adc_application, test_unified_sampling_preserves_digital_and_address_bit_order)
{
    const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    /* 低电平有效的 DI1/DI16，拨码 0110 对应地址 7。 */
    zassert_ok(gpio_emul_input_set_masked(gpio, BIT_MASK(21) & ~BIT(0),
        (BIT_MASK(17) & ~(BIT(0) | BIT(1) | BIT(16))) | BIT(17) | BIT(20)));
    struct acquisition_sample sample;
    zassert_ok(acquisition_inputs_sample(&sample));
    zassert_equal(sample.di, 0x8001);
    zassert_equal(sample.address, 7);
    zassert_equal(acquisition_inputs_sample(NULL), -EINVAL);
}

ZTEST(adc_application, test_independent_channel_groups_and_averaging)
{
    struct adc_dt_spec channels[] = {test_group.channels[2], test_group.channels[0]};
    const struct analog_adc_config group = {
        .channels = channels, .channel_count = ARRAY_SIZE(channels), .average_count = 3,
    };
    zassert_ok(analog_adc_init(&group));
    uint16_t values[3] = {0, 0, 0x55aa};
    zassert_ok(analog_adc_read(&group, values, 2));
    zassert_equal(values[0], 101);
    zassert_equal(values[1], 1301);
    zassert_equal(values[2], 0x55aa);
    zassert_equal(samples[0], 0);
}

ZTEST(adc_application, test_invalid_group_or_buffer_does_not_start_adc)
{
    uint16_t values[8];
    struct analog_adc_config group = test_group;
    zassert_equal(analog_adc_read(&group, NULL, 8), -EINVAL);
    zassert_equal(analog_adc_read(&group, values, 7), -ENOMEM);
    group.average_count = 0;
    zassert_equal(analog_adc_init(&group), -EINVAL);
    group = test_group;
    struct adc_dt_spec channel = group.channels[0];
    channel.channel_cfg.differential = true;
    group.channels = &channel;
    group.channel_count = 1;
    zassert_equal(analog_adc_init(&group), -ENOTSUP);
    for (unsigned int i = 0; i < ARRAY_SIZE(samples); ++i) { zassert_equal(samples[i], 0); }
}

ZTEST(adc_application, test_single_analog_group_propagates_read_failure)
{
    struct adc_dt_spec channel = test_group.channels[1];
    channel.resolution = 10;
    channel.vref_mv = 1800;
    const struct analog_adc_config config = {
        .channels = &channel, .channel_count = 1, .average_count = 3,
    };
    uint16_t raw;
    zassert_ok(analog_adc_init(&config));
    zassert_ok(analog_adc_read(&config, &raw, 1));
    zassert_equal(raw, 1);
    fail_channel = channel.channel_id;
    zassert_equal(analog_adc_read(&config, &raw, 1), -EIO);
}
ZTEST_SUITE(adc_application, NULL, NULL, before, NULL, NULL);
