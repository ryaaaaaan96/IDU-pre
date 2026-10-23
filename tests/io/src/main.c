#include <gpio/digital_io.h>
#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

/* A fault-injectable GPIO driver, exercised through the standard GPIO API. */
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
DEVICE_DEFINE(test_gpio, "test_gpio", NULL, NULL, &gpio_data, &gpio_config,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_gpio_api);

static const struct gpio_dt_spec pins[] = {
    {.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 2, .dt_flags = GPIO_ACTIVE_LOW},
    {.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 5, .dt_flags = GPIO_ACTIVE_HIGH},
    {.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 8, .dt_flags = GPIO_ACTIVE_LOW},
};
static struct watchdog monitors[2];
static const struct watchdog_config watchdogs[] = {
    {.feed = {.port = DEVICE_GET(test_gpio), .pin = 0},
     .feed_interval_ms = 5},
    {.feed = {.port = DEVICE_GET(test_gpio), .pin = 1},
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

ZTEST(io_modules, test_gpio_polarity_order_and_independent_groups)
{
    struct digital_io_config group = {
        .pins = pins, .count = ARRAY_SIZE(pins), .order = DIGITAL_IO_LSB_FIRST,
    };
    zassert_ok(digital_io_init(&group));
    const struct device *port = pins[0].port;
    /* Logical values 1,1,0 from physical levels 0,1,1. */
    zassert_ok(gpio_emul_input_set_masked(port, BIT(2) | BIT(5) | BIT(8), BIT(5) | BIT(8)));
    uint32_t value;
    zassert_ok(digital_io_read(&group, &value));
    zassert_equal(value, 3);
    group.order = DIGITAL_IO_MSB_FIRST;
    zassert_ok(digital_io_read(&group, &value));
    zassert_equal(value, 6);
    group.pins = &pins[1];
    group.count = 2;
    zassert_ok(digital_io_read(&group, &value));
    zassert_equal(value, 2);
}

ZTEST(io_modules, test_gpio_bounds_and_read_failure_preserve_output)
{
    struct digital_io_config group = {.pins = pins, .count = 33};
    uint32_t value = 0x55aa;
    zassert_equal(digital_io_init(&group), -EINVAL);
    group.count = 0;
    zassert_equal(digital_io_read(&group, &value), -EINVAL);
    group.count = 1;
    group.order = 99;
    zassert_equal(digital_io_read(&group, &value), -EINVAL);
    const struct gpio_dt_spec broken[] = {{.port = DEVICE_GET(test_gpio), .pin = 0}};
    group = (struct digital_io_config){.pins = broken, .count = 1};
    atomic_set(&configure_error, -EIO);
    zassert_equal(digital_io_init(&group), -EIO);
    atomic_set(&read_error, -EIO);
    zassert_equal(digital_io_read(&group, &value), -EIO);
    zassert_equal(value, 0x55aa);
    zassert_equal(digital_io_read(&group, NULL), -EINVAL);
    const struct gpio_dt_spec invalid = {.port = pins[0].port, .pin = 32};
    group.pins = &invalid;
    zassert_equal(digital_io_init(&group), -EINVAL);
}

ZTEST(io_modules, test_gpio_full_32_bit_group)
{
    struct gpio_dt_spec bank[32];
    for (size_t i = 0; i < ARRAY_SIZE(bank); ++i) {
        bank[i] = (struct gpio_dt_spec){.port = pins[0].port, .pin = i};
    }
    const struct digital_io_config group = {.pins = bank, .count = ARRAY_SIZE(bank)};
    zassert_ok(digital_io_init(&group));
    zassert_ok(gpio_emul_input_set_masked(pins[0].port, UINT32_MAX, 0x80000001));
    uint32_t value;
    zassert_ok(digital_io_read(&group, &value));
    zassert_equal(value, 0x80000001);
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
