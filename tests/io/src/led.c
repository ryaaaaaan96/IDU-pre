#include <led_indicator/led_indicator.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static atomic_t failure, values[2], writes[2];
static int brightness(const struct device *dev, uint32_t index, uint8_t value)
{
    ARG_UNUSED(dev);
    if (index >= ARRAY_SIZE(values)) { return -EINVAL; }
    if (atomic_get(&failure)) { return atomic_get(&failure); }
    atomic_set(&values[index], value > 0);
    atomic_inc(&writes[index]);
    return 0;
}
static DEVICE_API(led, fake_led_api) = {.set_brightness = brightness};
DEVICE_DEFINE(fake_led, "fake_led", NULL, NULL, NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_led_api);

static struct led_indicator indicators[2], gpio_indicator;
static const struct led_dt_spec outputs[] = {
    {.dev = DEVICE_GET(fake_led), .index = 0},
    {.dev = DEVICE_GET(fake_led), .index = 1},
};
static const struct led_dt_spec gpio_output = LED_DT_SPEC_GET(DT_NODELABEL(test_led));

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    atomic_clear(&failure);
    for (size_t i = 0; i < ARRAY_SIZE(indicators); ++i) {
        if (!indicators[i].initialized) {
            zassert_ok(led_indicator_init(&indicators[i], &outputs[i]));
        }
        zassert_ok(led_indicator_set(&indicators[i], false));
        atomic_clear(&writes[i]);
    }
    if (!gpio_indicator.initialized) {
        zassert_ok(led_indicator_init(&gpio_indicator, &gpio_output));
    }
}

static void after(void *fixture)
{
    ARG_UNUSED(fixture);
    atomic_clear(&failure);
    for (size_t i = 0; i < ARRAY_SIZE(indicators); ++i) {
        zassert_ok(led_indicator_set(&indicators[i], false));
    }
}

ZTEST(led_module, test_standard_led_driver_preserves_gpio_polarity)
{
    const struct device *port = DEVICE_DT_GET(DT_NODELABEL(led_gpio));
    zassert_ok(led_indicator_set(&gpio_indicator, true));
    zassert_equal(gpio_emul_output_get(port, 0), 0);
    zassert_ok(led_indicator_toggle(&gpio_indicator));
    zassert_equal(gpio_emul_output_get(port, 0), 1);
    zassert_ok(led_indicator_toggle(&gpio_indicator));
    zassert_equal(gpio_emul_output_get(port, 0), 0);
}

ZTEST(led_module, test_blink_timing_and_set_cancels_future_changes)
{
    zassert_ok(led_indicator_blink(&indicators[0], 20, 40));
    zassert_equal(atomic_get(&values[0]), 1);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&values[0]), 0);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&values[0]), 1);
    zassert_ok(led_indicator_set(&indicators[0], false));
    atomic_val_t count = atomic_get(&writes[0]);
    k_sleep(K_MSEC(100));
    zassert_equal(atomic_get(&writes[0]), count);
    zassert_equal(atomic_get(&values[0]), 0);
}

ZTEST(led_module, test_instances_have_independent_patterns)
{
    zassert_ok(led_indicator_blink(&indicators[0], 10, 10));
    zassert_ok(led_indicator_blink(&indicators[1], 15, 15));
    k_sleep(K_MSEC(40));
    zassert_ok(led_indicator_set(&indicators[0], true));
    atomic_val_t stopped = atomic_get(&writes[0]), active = atomic_get(&writes[1]);
    k_sleep(K_MSEC(45));
    zassert_equal(atomic_get(&writes[0]), stopped);
    zassert_true(atomic_get(&writes[1]) > active);
}

ZTEST(led_module, test_delayed_callback_does_not_truncate_replacement_pattern)
{
    zassert_ok(led_indicator_blink(&indicators[0], 5, 5));
    k_mutex_lock(&indicators[0].lock, K_FOREVER);
    k_sleep(K_MSEC(15)); /* Old work is blocked on the lock. */
    zassert_ok(led_indicator_blink(&indicators[0], 60, 100));
    k_mutex_unlock(&indicators[0].lock);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&values[0]), 1);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&values[0]), 0);
}

ZTEST(led_module, test_async_error_stops_blink_until_explicit_command)
{
    zassert_ok(led_indicator_blink(&indicators[0], 10, 10));
    atomic_set(&failure, -EIO);
    k_sleep(K_MSEC(20));
    zassert_equal(led_indicator_error(&indicators[0]), -EIO);
    atomic_val_t count = atomic_get(&writes[0]);
    atomic_clear(&failure);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&writes[0]), count);
    zassert_equal(led_indicator_error(&indicators[0]), -EIO);
    zassert_ok(led_indicator_set(&indicators[0], false));
    zassert_ok(led_indicator_error(&indicators[0]));
}

ZTEST(led_module, test_invalid_configuration_and_failed_initialization)
{
    static struct led_indicator uninitialized;
    zassert_equal(led_indicator_set(&uninitialized, true), -EACCES);
    zassert_equal(led_indicator_blink(&indicators[0], 0, 10), -EINVAL);
    zassert_equal(led_indicator_blink(&indicators[0], 10, UINT32_MAX), -EINVAL);
    zassert_equal(led_indicator_init(&indicators[0], &outputs[0]), -EALREADY);
    struct led_dt_spec invalid = {.dev = DEVICE_GET(fake_led), .index = 2};
    zassert_equal(led_indicator_init(&uninitialized, &invalid), -EINVAL);
    atomic_set(&failure, -EIO);
    zassert_equal(led_indicator_init(&uninitialized, &outputs[0]), -EIO);
    zassert_false(uninitialized.initialized);
    zassert_equal(atomic_get(&writes[0]), 0);
}

ZTEST_SUITE(led_module, NULL, NULL, before, after, NULL);
