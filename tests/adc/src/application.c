#include <zephyr/ztest.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <acquisition/acquisition.h>

#include "protocol/inc/signal_ids.h"
#include <zephyr/sys/byteorder.h>

static uint16_t di_value, current_values[SAMPLE_ANALOG_COUNT];
static unsigned updates;
static int set_error;
int32_t dp_set_para(uint16_t device, uint16_t signal, uint8_t size, uint8_t *bytes)
{
    zassert_equal(device, DEVICE_LOCAL);
    zassert_equal(size, 2);
    if (set_error) { return set_error; }
    if (signal == SIGNAL_DI) { di_value = sys_get_le16(bytes); }
    else {
        zassert_true(signal >= SIGNAL_CURRENT_1 && signal < SIGNAL_CURRENT_1 + SAMPLE_ANALOG_COUNT);
        current_values[signal - SIGNAL_CURRENT_1] = sys_get_le16(bytes);
    }
    ++updates;
    return 0;
}

static const struct device *const emul = DEVICE_DT_GET(DT_NODELABEL(adc0));
static unsigned int samples[16];
static int fail_channel;
static int injected_raw;
static int sample_value(const struct device *dev, unsigned int channel,
                        void *context, uint32_t *result)
{
    if ((int)channel == fail_channel) { return -EIO; }
    *result = injected_raw >= 0 ? injected_raw : channel * 100 + samples[channel];
    ++samples[channel];
    return 0;
}
static void before(void *fixture)
{
    memset(samples, 0, sizeof(samples));
    memset(current_values, 0, sizeof(current_values));
    updates = 0;
    set_error = 0;
    di_value = 0;
    fail_channel = -1;
    injected_raw = -1;
    for (unsigned int channel = 0; channel < 16; ++channel) {
        zassert_ok(adc_emul_raw_value_func_set(emul, channel, sample_value, NULL));
    }
    zassert_ok(acquisition_inputs_init());
}
ZTEST(adc_application, test_original_order_and_ten_sample_integer_average)
{
    const uint8_t order[] = {13, 0, 1, 2, 3, 4, 11, 12};
    const uint16_t expected[] = {21887, 67, 1745, 3424, 5102, 6781, 18530, 20208};
    zassert_ok(acquisition_cycle());
    zassert_equal(updates % 9, 0);
    for (unsigned int i = 0; i < ARRAY_SIZE(order); ++i) {
        zassert_equal(samples[order[i]], 10);
        zassert_equal(current_values[i], expected[i]);
    }
    zassert_equal(samples[5], 0);
}
ZTEST(adc_application, test_sampling_error_is_propagated_and_retry_works)
{
    fail_channel = 2;
    zassert_equal(acquisition_cycle(), -EIO);
    zassert_equal(updates, 4); /* DI 和前三路 AI 已写入，故障通道及后续点未更新。 */
    zassert_equal(samples[3], 0);
    fail_channel = -1;
    zassert_ok(acquisition_cycle());
    zassert_equal(updates, 13);
}

ZTEST(adc_application, test_digital_inputs_preserve_bit_order)
{
    const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    /* 低电平有效的 DI1/DI16 合成为位图。 */
    zassert_ok(gpio_emul_input_set_masked(gpio, BIT_MASK(17) & ~BIT(0),
        BIT_MASK(17) & ~(BIT(0) | BIT(1) | BIT(16))));
    zassert_ok(acquisition_cycle());
    zassert_equal(di_value, 0x8001);
}

ZTEST(adc_application, test_each_digital_input_maps_to_its_bit)
{
    const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    const uint32_t mask = BIT_MASK(17) & ~BIT(0);
    for (size_t i = 0; i < 16; ++i) {
        zassert_ok(gpio_emul_input_set_masked(gpio, mask, mask & ~BIT(i + 1)));
        zassert_ok(acquisition_cycle());
        zassert_equal(di_value, BIT(i));
    }
}

ZTEST(adc_application, test_point_write_failure_stops_before_adc_reads)
{
    set_error = -ENOMEM;
    zassert_equal(acquisition_cycle(), -ENOMEM);
    zassert_equal(updates, 0);
    for (size_t i = 0; i < ARRAY_SIZE(samples); ++i) { zassert_equal(samples[i], 0); }
}
ZTEST(adc_application, test_current_conversion_zero_midpoint_and_saturation)
{
    const uint16_t raw[] = {0, 1, 2048, 3904, 3905, 4095};
    const uint16_t expected[] = {0, 16, 34375, 65527, 65535, 65535};
    for (size_t n = 0; n < ARRAY_SIZE(raw); ++n) {
        injected_raw = raw[n];
        zassert_ok(acquisition_cycle());
        for (size_t i = 0; i < ARRAY_SIZE(current_values); ++i) {
            zassert_equal(current_values[i], expected[n]);
        }
    }
}
ZTEST_SUITE(adc_application, NULL, NULL, before, NULL, NULL);
