#include "status/inc/status.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>
#include "nsi_cpu_es_if.h"

static atomic_t edges;
void protocol_stop(void) {}
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
        status_sleep_forever(expected) == -ECANCELED &&
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
    zassert_ok(status_init());
    status_set_phase(STATUS_RUN);
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
    status_set_phase(STATUS_RUN);
    zassert_ok(status_progress());
}
ZTEST(task_watchdog, test_registration_validation_without_fixed_capacity)
{
    struct status_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_equal(status_feed(&extra), -EACCES);
    zassert_equal(status_sleep_forever(&extra), -EACCES);
    struct status_task_snapshot unregistered = {.name = "unchanged", .flags = UINT32_MAX};
    zassert_equal(status_task_get(&extra, &unregistered), -ENOENT);
    zassert_str_equal(unregistered.name, "unchanged");
    zassert_equal(unregistered.flags, UINT32_MAX);
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
    zassert_ok(status_register(&extra, &config));
    static struct status_task additional[16];
    for (size_t i = 0; i < ARRAY_SIZE(additional); ++i) {
        zassert_ok(status_register(&additional[i], &config));
        zassert_ok(status_feed(&additional[i]));
    }
    for (size_t i = 0; i < ARRAY_SIZE(additional); ++i) {
        zassert_ok(status_sleep_forever(&additional[i]));
    }
    zassert_equal(status_task_get(NULL, &after), -EINVAL);
    zassert_equal(status_fault_get(NULL), -EINVAL);
}
ZTEST(task_watchdog, test_explicit_hold_keeps_feed_without_task_fault)
{
    const struct status_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_ok(status_register(&first, &config));
    (void)status_report_fault(STATUS_INIT, -EIO, "Input initialization failed", STATUS_ACTION_HOLD);
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_true(atomic_get(&edges) > before);
    zassert_equal(status_feed(&first), -ECANCELED);
    zassert_equal(status_register(&extra, &config), -ECANCELED);
    struct status_fault_snapshot fault;
    zassert_equal(status_fault_get(&fault), -ENOENT);
}
ZTEST(task_watchdog, test_permanent_sleep_and_explicit_reregistration)
{
    struct status_task_config config = {.name = "sample", .timeout_ms = 100};
    zassert_ok(status_register(&first, &config));
    k_sleep(K_MSEC(30));
    zassert_ok(status_feed(&first));
    struct status_task_snapshot before, sleeping, resumed;
    zassert_ok(status_task_get(&first, &before));
    zassert_ok(status_sleep_forever(&first));
    zassert_ok(status_sleep_forever(&first));
    zassert_equal(status_feed(&first), -EACCES);
    zassert_ok(status_task_get(&first, &sleeping));
    zassert_equal(sleeping.flags, STATUS_TASK_SLEEP_FOREVER);
    zassert_equal(sleeping.last_feed_ms, before.last_feed_ms);
    atomic_val_t edges_before = atomic_get(&edges);
    k_sleep(K_MSEC(250));
    zassert_true(atomic_get(&edges) > edges_before);
    config.name = "resumed"; config.timeout_ms = 200;
    zassert_ok(status_register(&first, &config));
    zassert_ok(status_task_get(&first, &resumed));
    zassert_str_equal(resumed.name, "resumed");
    zassert_equal(resumed.timeout_ms, 200);
    zassert_equal(resumed.flags, STATUS_TASK_ACTIVE);
    zassert_true(resumed.last_feed_ms > sleeping.last_feed_ms);
    k_sleep(K_MSEC(120));
    zassert_ok(status_feed(&first));
    zassert_ok(status_sleep_forever(&first));
}
ZTEST(task_watchdog, test_invalid_resume_preserves_sleep_and_other_tasks)
{
    struct status_task_config config = {.name = "sleeping", .timeout_ms = 100};
    zassert_ok(status_register(&first, &config));
    zassert_ok(status_sleep_forever(&first));
    config.name = "peer"; config.timeout_ms = 1000;
    zassert_ok(status_register(&second, &config));
    zassert_ok(status_register(&extra, &config));
    config.name = "resume"; config.timeout_ms = 0;
    zassert_equal(status_register(&first, &config), -EINVAL);
    struct status_task_snapshot state;
    zassert_ok(status_task_get(&first, &state));
    zassert_str_equal(state.name, "sleeping");
    zassert_equal(state.timeout_ms, 100);
    zassert_equal(state.flags, STATUS_TASK_SLEEP_FOREVER);
    zassert_equal(status_feed(&first), -EACCES);
    zassert_ok(status_sleep_forever(&second));
    config.timeout_ms = 300;
    zassert_ok(status_register(&first, &config));
    zassert_equal(status_feed(&second), -EACCES);
    k_sleep(K_MSEC(180));
    zassert_ok(status_feed(&first));
    zassert_ok(status_feed(&extra));
    zassert_ok(status_sleep_forever(&first));
    zassert_ok(status_sleep_forever(&extra));
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(1100));
    zassert_true(atomic_get(&edges) > before);
}
ZTEST(task_watchdog, test_resumed_deadline_stops_and_reboots)
{
    struct status_task_config config = {.name = "old", .timeout_ms = 1000};
    zassert_ok(status_register(&first, &config));
    zassert_ok(status_sleep_forever(&first));
    config.name = "peer";
    zassert_ok(status_register(&second, &config));
    config.name = "silent"; config.timeout_ms = 100;
    zassert_ok(status_register(&first, &config));
    expected = &first;
    k_sleep(K_MSEC(300));
    zassert_unreachable("Resumed task did not time out");
}
ZTEST(task_watchdog, test_sleep_does_not_protect_peer_stops_and_reboots)
{
    struct status_task_config config = {.name = "sleeping", .timeout_ms = 100};
    zassert_ok(status_register(&second, &config));
    zassert_ok(status_sleep_forever(&second));
    config.name = "silent"; config.timeout_ms = 150;
    zassert_ok(status_register(&first, &config));
    expected = &first;
    k_sleep(K_MSEC(300));
    zassert_unreachable("Peer task did not time out");
}

ZTEST(task_watchdog, test_feeding_earliest_preserves_peer_deadline_stops_and_reboots)
{
    struct status_task_config config = {.name = "fed", .timeout_ms = 80};
    zassert_ok(status_register(&first, &config));
    config.name = "silent"; config.timeout_ms = 120;
    zassert_ok(status_register(&second, &config));
    expected = &second;
    k_sleep(K_MSEC(60));
    zassert_ok(status_feed(&first));
    k_sleep(K_MSEC(200));
    zassert_unreachable("Feeding the earliest task hid its peer deadline");
}

ZTEST(task_watchdog, test_sleeping_earliest_preserves_peer_deadline_stops_and_reboots)
{
    struct status_task_config config = {.name = "sleeping", .timeout_ms = 80};
    zassert_ok(status_register(&first, &config));
    config.name = "silent"; config.timeout_ms = 120;
    zassert_ok(status_register(&second, &config));
    expected = &second;
    zassert_ok(status_sleep_forever(&first));
    k_sleep(K_MSEC(100));
    struct status_task_snapshot snapshot;
    zassert_ok(status_task_get(&second, &snapshot));
    zassert_equal(snapshot.flags, STATUS_TASK_ACTIVE);
    k_sleep(K_MSEC(100));
    zassert_unreachable("Sleeping the earliest task cancelled its peer deadline");
}

static struct k_thread worker;
K_THREAD_STACK_DEFINE(worker_stack, 2048);
K_SEM_DEFINE(worker_done, 0, 1);
static void change_registration(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (int i = 0; i < 40; ++i) {
        const struct status_task_config config = {
            .name = i % 2 ? "slow" : "fast", .timeout_ms = i % 2 ? 600 : 300,
        };
        zassert_ok(status_sleep_forever(&first));
        zassert_ok(status_register(&first, &config));
        zassert_ok(status_feed(&first));
        k_sleep(K_MSEC(1));
    }
    zassert_ok(status_sleep_forever(&first));
    k_sem_give(&worker_done);
}
ZTEST(task_watchdog, test_concurrent_snapshots_are_consistent)
{
    const struct status_task_config config = {.name = "fast", .timeout_ms = 300};
    zassert_ok(status_register(&first, &config));
    k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                    change_registration, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    int64_t deadline = k_uptime_get() + 1500;
    unsigned snapshots = 0;
    while (k_sem_take(&worker_done, K_NO_WAIT)) {
        struct status_task_snapshot state;
        zassert_ok(status_task_get(&first, &state));
        zassert_true((!strcmp(state.name, "fast") && state.timeout_ms == 300) ||
                     (!strcmp(state.name, "slow") && state.timeout_ms == 600));
        zassert_true(state.flags == STATUS_TASK_ACTIVE || state.flags == STATUS_TASK_SLEEP_FOREVER);
        zassert_true(k_uptime_get() < deadline);
        ++snapshots;
        k_sleep(K_MSEC(1));
    }
    zassert_ok(k_thread_join(&worker, K_MSEC(100)));
    zassert_true(snapshots > 0);
}
ZTEST_SUITE(task_watchdog, NULL, setup, NULL, NULL, NULL);
