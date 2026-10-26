#include <zephyr/ztest.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <acquisition/acquisition.h>

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

ZTEST_SUITE(adc_application, NULL, NULL, before, NULL, NULL);
