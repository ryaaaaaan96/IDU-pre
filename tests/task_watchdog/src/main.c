#include "status/status.h"
#include "status/task_watchdog/task_watchdog.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>
#include "nsi_cpu_es_if.h"

static atomic_t edges;
static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = UINT32_MAX};
static int configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{ ARG_UNUSED(dev); ARG_UNUSED(pin); ARG_UNUSED(flags); return 0; }
static int set(const struct device *dev, gpio_port_pins_t mask, gpio_port_value_t value)
{ ARG_UNUSED(dev); ARG_UNUSED(mask); ARG_UNUSED(value); return 0; }
static int set_bits(const struct device *dev, gpio_port_pins_t pins)
{ ARG_UNUSED(dev); ARG_UNUSED(pins); return 0; }
static int toggle(const struct device *dev, gpio_port_pins_t pins)
{ ARG_UNUSED(dev); if (pins & BIT(21)) { atomic_inc(&edges); } return 0; }
static DEVICE_API(gpio, gpio_api) = {
    .pin_configure = configure, .port_set_masked_raw = set, .port_toggle_bits = toggle,
    .port_set_bits_raw = set_bits, .port_clear_bits_raw = set_bits,
};
DEVICE_DT_DEFINE(DT_NODELABEL(gpio0), NULL, NULL, &gpio_data, &gpio_config,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &gpio_api);
static struct application_status application;
static struct status_task first, second, extra;
static struct status_task *expected;

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    struct status_fault_snapshot fault, after;
    struct status_task_snapshot task;
    const struct status_task_config config = {.name = "late", .timeout_ms = 100};
    bool ok = expected && type == SYS_REBOOT_COLD &&
        !status_fault_get(&fault) && !status_task_get(expected, &task) &&
        fault.error == -ETIMEDOUT && fault.feed_error == -ECANCELED &&
        !strcmp(fault.task.name, "silent") && (task.flags & STATUS_TASK_FAULT) &&
        status_feed(expected) == -ECANCELED &&
        status_register(&extra, &config) == -ECANCELED &&
        !status_fault_get(&after) && after.occurred_ms == fault.occurred_ms;
    printk("WATCHDOG_RESET_%s\n", ok ? "PASS" : "FAIL");
    nsi_exit(ok ? 0 : 1);
    CODE_UNREACHABLE;
}
static void *setup(void)
{
    const struct status_task_config config = {.name = "early", .timeout_ms = 100};
    zassert_equal(status_register(&extra, &config), -EACCES);
    zassert_ok(status_init(&application));
    return NULL;
}
ZTEST(task_watchdog, test_registration_and_independent_heartbeats)
{
    const struct status_task_config config = {.name = "sample", .timeout_ms = 100};
    zassert_ok(status_register(&first, &config));
    for (int i = 0; i < 5; ++i) {
        k_sleep(K_MSEC(40));
        zassert_ok(status_feed(&first));
    }
    struct status_task_snapshot snapshot;
    zassert_ok(status_task_get(&first, &snapshot));
    zassert_equal(snapshot.timeout_ms, 100);
    zassert_true(snapshot.flags & STATUS_TASK_ACTIVE);
    zassert_true(snapshot.last_feed_ms >= 200);
    zassert_true(atomic_get(&edges) > 0);
}
ZTEST(task_watchdog, test_one_missing_heartbeat_stops_and_reboots)
{
    const struct status_task_config silent = {.name = "silent", .timeout_ms = 100};
    const struct status_task_config busy = {.name = "busy", .timeout_ms = 200};
    zassert_ok(status_register(&first, &silent));
    zassert_ok(status_register(&second, &busy));
    expected = &first;
    for (int i = 0; i < 10; ++i) {
        k_sleep(K_MSEC(30));
        zassert_ok(status_feed(&second));
    }
    zassert_unreachable("Missing heartbeat did not reboot");
}
ZTEST(task_watchdog, test_zero_registered_continues_past_legacy_timeout)
{
    k_sleep(K_MSEC(1700));
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_true(atomic_get(&edges) > before);
    struct status_fault_snapshot fault;
    zassert_equal(status_fault_get(&fault), -ENOENT);
    status_set_phase(&application, STATUS_CHECKING_HEALTH);
    zassert_ok(status_progress(&application));
}
ZTEST(task_watchdog, test_registration_validation_and_capacity)
{
    struct status_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_equal(status_feed(&extra), -EACCES);
    zassert_equal(status_register(NULL, &config), -EINVAL);
    zassert_equal(status_register(&extra, NULL), -EINVAL);
    config.name = NULL;
    zassert_equal(status_register(&extra, &config), -EINVAL);
    config.name = "";
    zassert_equal(status_register(&extra, &config), -EINVAL);
    config.name = "first"; config.timeout_ms = 0;
    zassert_equal(status_register(&extra, &config), -EINVAL);
    config.timeout_ms = 2000;
    zassert_ok(status_register(&first, &config));
    struct status_task_snapshot before, after;
    zassert_ok(status_task_get(&first, &before));
    k_sleep(K_MSEC(20));
    config.name = "changed"; config.timeout_ms = 100;
    zassert_equal(status_register(&first, &config), -EALREADY);
    zassert_ok(status_task_get(&first, &after));
    zassert_str_equal(after.name, "first");
    zassert_equal(after.timeout_ms, 2000);
    zassert_equal(after.last_feed_ms, before.last_feed_ms);
    config.timeout_ms = 2000;
    zassert_ok(status_register(&second, &config));
    zassert_equal(status_register(&extra, &config), -ENOMEM);
    zassert_equal(status_task_get(&extra, &after), -ENOENT);
    zassert_equal(status_task_get(NULL, &after), -EINVAL);
    zassert_equal(status_fault_get(NULL), -EINVAL);
}
ZTEST(task_watchdog, test_explicit_fault_stays_stopped_with_no_task_fault)
{
    const struct status_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_ok(status_register(&first, &config));
    status_fail(&application, STATUS_STARTING, -EIO);
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&edges), before);
    zassert_equal(status_feed(&first), -ECANCELED);
    zassert_equal(status_register(&extra, &config), -ECANCELED);
    struct status_fault_snapshot fault;
    zassert_equal(status_fault_get(&fault), -ENOENT);
}
ZTEST_SUITE(task_watchdog, NULL, setup, NULL, NULL, NULL);
