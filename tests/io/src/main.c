#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

/* 可注入故障的 GPIO 驱动，通过标准 GPIO 接口测试。 */
static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = UINT32_MAX};
static atomic_t toggle_error, configure_error, read_error;
static atomic_t toggles[2];
static gpio_flags_t output_flags[2];

static int configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(dev);
    if (atomic_get(&configure_error)) { return atomic_get(&configure_error); }
    if (pin < ARRAY_SIZE(output_flags)) { output_flags[pin] = flags; }
    return 0;
}

static int toggle(const struct device *dev, gpio_port_pins_t pins)
{
    ARG_UNUSED(dev);
    if (atomic_get(&toggle_error)) { return atomic_get(&toggle_error); }
    for (size_t i = 0; i < ARRAY_SIZE(toggles); ++i) {
        if (pins & BIT(i)) { atomic_inc(&toggles[i]); }
    }
    return 0;
}

static int read_port(const struct device *dev, gpio_port_value_t *value)
{
    ARG_UNUSED(dev);
    if (atomic_get(&read_error)) { return atomic_get(&read_error); }
    *value = 0;
    return 0;
}

static DEVICE_API(gpio, test_gpio_api) = {
    .pin_configure = configure, .port_toggle_bits = toggle, .port_get_raw = read_port,
};
DEVICE_DT_DEFINE(DT_NODELABEL(gpio0), NULL, NULL, &gpio_data, &gpio_config,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_gpio_api);

static struct watchdog monitors[2];
static const struct watchdog_config watchdogs[] = {
    {.feed = {.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 0},
     .feed_interval_ms = 5},
    {.feed = {.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 1},
     .feed_interval_ms = 5},
};

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(monitors, 0, sizeof(monitors));
    memset(output_flags, 0, sizeof(output_flags));
    for (size_t i = 0; i < ARRAY_SIZE(toggles); ++i) { atomic_clear(&toggles[i]); }
    atomic_clear(&toggle_error);
    atomic_clear(&configure_error);
    atomic_clear(&read_error);
}

static void after(void *fixture)
{
    ARG_UNUSED(fixture);
    for (size_t i = 0; i < ARRAY_SIZE(monitors); ++i) { watchdog_stop(&monitors[i]); }
}

ZTEST(io_modules, test_no_registration_keeps_feeding_past_old_deadline)
{
    zassert_ok(watchdog_init(&monitors[0], &watchdogs[0]));
    k_sleep(K_MSEC(80));
    atomic_val_t before = atomic_get(&toggles[0]);
    k_sleep(K_MSEC(20));
    zassert_true(atomic_get(&toggles[0]) > before);
}

ZTEST(io_modules, test_watchdog_feeds_until_explicit_stop_and_keeps_pin_driven)
{
    zassert_ok(watchdog_init(&monitors[0], &watchdogs[0]));
    for (int i = 0; i < 6; ++i) {
        k_sleep(K_MSEC(10));
        zassert_ok(watchdog_error(&monitors[0]));
    }
    zassert_true(atomic_get(&toggles[0]) > 0);
    watchdog_stop(&monitors[0]);
    atomic_val_t stopped = atomic_get(&toggles[0]);
    k_sleep(K_MSEC(15));
    zassert_equal(atomic_get(&toggles[0]), stopped);
    zassert_true(output_flags[0] & GPIO_OUTPUT);
    zassert_equal(watchdog_error(&monitors[0]), -ECANCELED);
    zassert_equal(watchdog_init(&monitors[0], &watchdogs[0]), -EALREADY);
}

ZTEST(io_modules, test_stopping_one_instance_does_not_stop_another)
{
    zassert_ok(watchdog_init(&monitors[0], &watchdogs[0]));
    zassert_ok(watchdog_init(&monitors[1], &watchdogs[1]));
    watchdog_stop(&monitors[0]);
    k_sleep(K_MSEC(80));
    zassert_equal(atomic_get(&toggles[0]), 0);
    zassert_true(atomic_get(&toggles[1]) > 0);
    zassert_equal(watchdog_error(&monitors[0]), -ECANCELED);
    zassert_ok(watchdog_error(&monitors[1]));
}

ZTEST(io_modules, test_gpio_feed_failure_latches_and_does_not_resume)
{
    zassert_ok(watchdog_init(&monitors[0], &watchdogs[0]));
    atomic_set(&toggle_error, -EIO);
    k_sleep(K_MSEC(10));
    zassert_equal(watchdog_error(&monitors[0]), -EIO);
    atomic_clear(&toggle_error);
    k_sleep(K_MSEC(10));
    zassert_equal(atomic_get(&toggles[0]), 0);
    zassert_equal(watchdog_error(&monitors[0]), -EIO);
}

ZTEST(io_modules, test_watchdog_invalid_configuration_and_startup_failure)
{
    struct watchdog_config config = watchdogs[0];
    config.feed_interval_ms = 0;
    zassert_equal(watchdog_init(&monitors[0], &config), -EINVAL);
    config = watchdogs[0];
    config.feed.pin = 32;
    zassert_equal(watchdog_init(&monitors[0], &config), -EINVAL);
    zassert_equal(watchdog_error(&monitors[0]), -EACCES);
    atomic_set(&configure_error, -EIO);
    zassert_equal(watchdog_init(&monitors[0], &watchdogs[0]), -EIO);
    k_sleep(K_MSEC(10));
    zassert_equal(atomic_get(&toggles[0]), 0);
}

ZTEST_SUITE(io_modules, NULL, NULL, before, after, NULL);
