#include <zephyr/ztest.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <acquisition/acquisition.h>
#include <analog_input/analog_input.h>
#include <input_sampler/input_sampler.h>

#define TEST_ADC_SPEC(node, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node, idx),
static const struct adc_dt_spec test_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_NODELABEL(idu_io), io_channels, TEST_ADC_SPEC)
};
static const struct analog_input_config test_group = {
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
    const struct analog_input_config group = {
        .channels = channels, .channel_count = ARRAY_SIZE(channels), .average_count = 3,
    };
    zassert_ok(analog_input_init(&group));
    uint16_t values[3] = {0, 0, 0x55aa};
    zassert_ok(analog_input_read(&group, values, 2));
    zassert_equal(values[0], 101);
    zassert_equal(values[1], 1301);
    zassert_equal(values[2], 0x55aa);
    zassert_equal(samples[0], 0);
}

ZTEST(adc_application, test_invalid_group_or_buffer_does_not_start_adc)
{
    uint16_t values[8];
    struct analog_input_config group = test_group;
    zassert_equal(analog_input_read(&group, NULL, 8), -EINVAL);
    zassert_equal(analog_input_read(&group, values, 7), -ENOMEM);
    group.average_count = 0;
    zassert_equal(analog_input_init(&group), -EINVAL);
    group = test_group;
    struct adc_dt_spec channel = group.channels[0];
    channel.channel_cfg.differential = true;
    group.channels = &channel;
    group.channel_count = 1;
    zassert_equal(analog_input_init(&group), -ENOTSUP);
    for (unsigned int i = 0; i < ARRAY_SIZE(samples); ++i) { zassert_equal(samples[i], 0); }
}

ZTEST(adc_application, test_generic_sampler_uses_caller_layout_and_buffers)
{
    struct adc_dt_spec channels[] = {test_group.channels[2], test_group.channels[0]};
    const struct analog_input_config analog = {
        .channels = channels, .channel_count = ARRAY_SIZE(channels), .average_count = 3,
    };
    const struct gpio_dt_spec pin = {
        .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 2, .dt_flags = GPIO_ACTIVE_LOW,
    };
    const struct digital_io_config digital = {
        .pins = &pin, .count = 1, .order = DIGITAL_IO_LSB_FIRST,
    };
    const struct input_sampler_config config = {
        .analog = &analog, .digital_groups = &digital, .digital_group_count = 1,
    };
    uint16_t raw[] = {0, 0, 0x55aa};
    uint32_t bits[] = {0, 0xa55a};
    struct input_sampler_sample output = {
        .analog = raw, .analog_capacity = 2, .digital = bits, .digital_capacity = 1,
    };

    zassert_ok(input_sampler_init(&config));
    zassert_ok(gpio_emul_input_set(pin.port, pin.pin, 0));
    zassert_ok(input_sampler_read(&config, &output));
    zassert_true(output.valid);
    zassert_equal(raw[0], 101);
    zassert_equal(raw[1], 1301);
    zassert_equal(bits[0], 1);
    zassert_equal(raw[2], 0x55aa);
    zassert_equal(bits[1], 0xa55a);
    zassert_equal(samples[0], 0);
}

ZTEST(adc_application, test_generic_sampler_rejects_short_buffers_before_reading)
{
    const struct gpio_dt_spec pin = {
        .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 2, .dt_flags = GPIO_ACTIVE_LOW,
    };
    const struct digital_io_config digital = {
        .pins = &pin, .count = 1, .order = DIGITAL_IO_LSB_FIRST,
    };
    const struct input_sampler_config config = {
        .analog = &test_group, .digital_groups = &digital, .digital_group_count = 1,
    };
    uint16_t raw[8] = {0x55aa};
    uint32_t bits = 0xa55a;
    struct input_sampler_sample output = {
        .analog = raw, .analog_capacity = 7, .digital = &bits, .digital_capacity = 1,
        .valid = true,
    };

    zassert_equal(input_sampler_read(&config, &output), -ENOMEM);
    zassert_false(output.valid);
    output.analog_capacity = ARRAY_SIZE(raw);
    output.digital_capacity = 0;
    zassert_equal(input_sampler_read(&config, &output), -ENOMEM);
    zassert_equal(bits, 0xa55a);
    zassert_equal(raw[0], 0x55aa);
    output.digital_capacity = 1;
    output.digital = NULL;
    zassert_equal(input_sampler_read(&config, &output), -EINVAL);
    zassert_equal(input_sampler_read(NULL, &output), -EINVAL);
    zassert_equal(input_sampler_read(&config, NULL), -EINVAL);
    zassert_equal(input_sampler_init(NULL), -EINVAL);
    zassert_equal(input_sampler_init(&(struct input_sampler_config){0}), -EINVAL);
    for (size_t i = 0; i < ARRAY_SIZE(samples); ++i) { zassert_equal(samples[i], 0); }
}

ZTEST(adc_application, test_generic_sampler_supports_single_input_kind)
{
    struct adc_dt_spec channel = test_group.channels[1];
    channel.resolution = 10;
    channel.vref_mv = 1800;
    const struct analog_input_config analog = {
        .channels = &channel, .channel_count = 1, .average_count = 3,
    };
    struct input_sampler_config config = {
        .analog = &analog, .digital_groups = NULL, .digital_group_count = 0,
    };
    uint16_t raw;
    struct input_sampler_sample output = {.analog = &raw, .analog_capacity = 1};

    zassert_ok(input_sampler_init(&config));
    zassert_ok(input_sampler_read(&config, &output));
    zassert_true(output.valid);
    zassert_equal(raw, 1);
    fail_channel = channel.channel_id;
    zassert_equal(input_sampler_read(&config, &output), -EIO);
    zassert_false(output.valid);

    const struct gpio_dt_spec pin = {
        .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 2, .dt_flags = GPIO_ACTIVE_LOW,
    };
    const struct digital_io_config digital = {
        .pins = &pin, .count = 1, .order = DIGITAL_IO_LSB_FIRST,
    };
    config = (struct input_sampler_config){
        .analog = NULL, .digital_groups = &digital, .digital_group_count = 1,
    };
    uint32_t bits;
    output = (struct input_sampler_sample){.digital = &bits, .digital_capacity = 1};
    zassert_ok(input_sampler_init(&config));
    zassert_ok(gpio_emul_input_set(pin.port, pin.pin, 1));
    zassert_ok(input_sampler_read(&config, &output));
    zassert_true(output.valid);
    zassert_equal(bits, 0);
}
ZTEST_SUITE(adc_application, NULL, NULL, before, NULL, NULL);
