#include <zephyr/ztest.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include "hardware/idu_pre.h"

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
    zassert_ok(idu_analog_init());
}
ZTEST(adc_application, test_original_order_and_ten_sample_integer_average)
{
    const uint8_t order[] = {13, 0, 1, 2, 3, 4, 11, 12};
    uint16_t raw[8];
    zassert_ok(idu_analog_read(raw));
    for (unsigned int i = 0; i < ARRAY_SIZE(order); ++i) {
        zassert_equal(samples[order[i]], 10);
        zassert_equal(raw[i], order[i] * 100 + 4);
    }
    zassert_equal(samples[5], 0);
}
ZTEST(adc_application, test_sampling_error_is_propagated_and_retry_works)
{
    uint16_t raw[8];
    fail_channel = 2;
    zassert_equal(idu_analog_read(raw), -EIO);
    zassert_equal(samples[3], 0);
    fail_channel = -1;
    zassert_ok(idu_analog_read(raw));
}
ZTEST_SUITE(adc_application, NULL, NULL, before, NULL, NULL);
