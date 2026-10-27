#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>
#include <zephyr/irq_offload.h>

static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = UINT32_MAX};
static atomic_t toggle_error, configure_error, toggles, configurations, notifications;
static gpio_flags_t output_flags;
static bool callback_in_isr;
static struct watchdog_fault reported;

static int configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(dev); ARG_UNUSED(pin);
    atomic_inc(&configurations);
    if (atomic_get(&configure_error)) { return atomic_get(&configure_error); }
    output_flags = flags;
    return 0;
}
static int toggle(const struct device *dev, gpio_port_pins_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(pins);
    if (atomic_get(&toggle_error)) { return atomic_get(&toggle_error); }
    atomic_inc(&toggles);
    return 0;
}
static DEVICE_API(gpio, test_gpio_api) = {
    .pin_configure = configure, .port_toggle_bits = toggle,
};
DEVICE_DT_DEFINE(DT_NODELABEL(gpio0), NULL, NULL, &gpio_data, &gpio_config,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_gpio_api);
static int notify(const struct watchdog_fault *fault)
{
    reported = *fault;
    callback_in_isr = k_is_in_isr();
    atomic_inc(&notifications);
    return fault->action == WATCHDOG_ACTION_CONTINUE ? 0 : fault->error;
}

ZTEST(io_modules, test_explicit_start_stop_and_no_restart)
{
    k_sleep(K_MSEC(20));
    zassert_equal(atomic_get(&configurations), 0);
    zassert_equal(atomic_get(&toggles), 0);
    zassert_ok(watchdog_init(notify));
    k_sleep(K_MSEC(50));
    zassert_true(atomic_get(&toggles) > 0);
    watchdog_stop(WATCHDOG_STOP_MONITOR);
    atomic_val_t before = atomic_get(&toggles);
    k_sleep(K_MSEC(30));
    zassert_true(atomic_get(&toggles) > before);
    watchdog_stop(WATCHDOG_STOP_ALL);
    before = atomic_get(&toggles);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&toggles), before);
    zassert_true(output_flags & GPIO_OUTPUT);
    zassert_equal(watchdog_init(notify), -EALREADY);
}

ZTEST(io_modules, test_gpio_error_latches_and_notifies_from_workqueue)
{
    zassert_ok(watchdog_init(notify));
    atomic_set(&toggle_error, -EIO);
    k_sleep(K_MSEC(30));
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_GPIO);
    zassert_equal(fault.error, -EIO);
    zassert_equal(atomic_get(&notifications), 1);
    zassert_false(callback_in_isr);
    atomic_clear(&toggle_error);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&toggles), 0);
    zassert_equal(atomic_get(&notifications), 1);
}

ZTEST(io_modules, test_initialization_error_is_returned_without_retry)
{
    atomic_set(&configure_error, -EIO);
    zassert_equal(watchdog_init(notify), -EIO);
    zassert_equal(watchdog_init(notify), -EALREADY);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&configurations), 1);
    zassert_equal(atomic_get(&toggles), 0);
    zassert_equal(atomic_get(&notifications), 0);
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.error, -EIO);
}

ZTEST(io_modules, test_invalid_arguments_do_not_start_module)
{
    zassert_equal(watchdog_init(NULL), -EINVAL);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(NULL), -EINVAL);
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    zassert_equal(atomic_get(&configurations), 0);
    zassert_ok(watchdog_init(notify));
    watchdog_stop(WATCHDOG_STOP_ALL);
}

static void report_from_isr(const void *unused)
{
    ARG_UNUSED(unused);
    zassert_ok(watchdog_report_fault(-EIO, "ISR diagnostic", WATCHDOG_ACTION_CONTINUE));
}

/* 不链接 status，直接验证通用上报、同步回调和监控诊断互不干扰。 */
ZTEST(io_modules, test_report_is_synchronous_and_preserves_monitoring)
{
    zassert_equal(watchdog_report_fault(-EIO, "Before init", WATCHDOG_ACTION_HOLD), -EACCES);
    zassert_ok(watchdog_init(notify));
    zassert_ok(watchdog_report_fault(-EIO, "Diagnostic", WATCHDOG_ACTION_CONTINUE));
    zassert_equal(atomic_get(&notifications), 1);
    zassert_false(callback_in_isr);
    zassert_equal(reported.reason, WATCHDOG_FAULT_REPORTED);
    zassert_equal(reported.error, -EIO);
    zassert_equal(reported.action, WATCHDOG_ACTION_CONTINUE);
    zassert_equal(strcmp(reported.description, "Diagnostic"), 0);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    atomic_val_t before = atomic_get(&toggles);
    k_sleep(K_MSEC(30));
    zassert_true(atomic_get(&toggles) > before);
    irq_offload(report_from_isr, NULL);
    zassert_true(callback_in_isr);
    zassert_equal(atomic_get(&notifications), 2);
    /* stop 不解除回调绑定，终止后仍可记录新的故障。 */
    watchdog_stop(WATCHDOG_STOP_ALL);
    zassert_equal(watchdog_report_fault(-EIO, "After stop", WATCHDOG_ACTION_HOLD), -EIO);
    zassert_equal(atomic_get(&notifications), 3);
    zassert_equal(watchdog_report_fault(0, "Invalid", WATCHDOG_ACTION_HOLD), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, NULL, WATCHDOG_ACTION_HOLD), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "", WATCHDOG_ACTION_HOLD), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "Invalid", (watchdog_action_t)-1), -EINVAL);
    zassert_equal(atomic_get(&notifications), 3);
}

ZTEST_SUITE(io_modules, NULL, NULL, NULL, NULL, NULL);
