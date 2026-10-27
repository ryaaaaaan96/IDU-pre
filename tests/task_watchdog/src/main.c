#include <watchdog/watchdog.h>
#include "status/inc/status.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>
#include "nsi_cpu_es_if.h"
#ifndef CONFIG_WATCHDOG_CHECK_INTERVAL_MS
#define CONFIG_WATCHDOG_CHECK_INTERVAL_MS 100
#endif

static atomic_t edges, feed_configures;
static atomic_t protocol_stop_calls, last_stop_in_isr;
static atomic_t queue_blocked;
static bool expect_check_fault, expect_direct_reset;
static void block_queue(void);
void protocol_stop(void)
{
    atomic_inc(&protocol_stop_calls);
    atomic_set(&last_stop_in_isr, k_is_in_isr());
}
static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = UINT32_MAX};
static int configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{ ARG_UNUSED(dev); ARG_UNUSED(flags); if (pin == 21) {
    atomic_inc(&feed_configures);
    if (IS_ENABLED(CONFIG_TEST_WATCHDOG_INIT_FAILURE)) { return -EIO; }
} return 0; }
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
static struct watchdog_task first, second, extra;
static struct watchdog_task *expected;

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    struct watchdog_fault fault, after;
    struct watchdog_task_snapshot task;
    const struct watchdog_task_config config = {.name = "late", .timeout_ms = 100};
    atomic_val_t before = atomic_get(&edges);
    if (!k_is_in_isr()) { k_sleep(K_MSEC(30)); }
    bool ok = type == SYS_REBOOT_COLD && atomic_get(&edges) == before;
    if (expect_direct_reset) {
        ok = ok && watchdog_task_feed(&first) ==
             (IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE) ? -ECANCELED : -EACCES);
    } else {
        ok = ok && !k_is_in_isr() && !watchdog_fault_get(&fault) &&
             atomic_get(&protocol_stop_calls) > 0 && fault.error == -ETIMEDOUT;
        if (expect_check_fault) {
            ok = ok && fault.reason == WATCHDOG_FAULT_CHECK && !fault.task.name;
        } else {
            ok = ok && expected && !watchdog_task_get(expected, &task) &&
                 fault.reason == WATCHDOG_FAULT_TASK &&
                 !strcmp(fault.task.name, "silent") && (task.flags & WATCHDOG_TASK_FAULT) &&
                 watchdog_task_feed(expected) == -ECANCELED &&
                 watchdog_task_sleep_forever(expected) == -ECANCELED;
        }
        ok = ok && watchdog_task_register(&extra, &config) == -ECANCELED &&
             !watchdog_fault_get(&after) && after.occurred_ms == fault.occurred_ms;
    }
    printk("WATCHDOG_RESET_%s\n", ok ? "PASS" : "FAIL");
    nsi_exit(ok ? 0 : 1);
    CODE_UNREACHABLE;
}
static void *setup(void)
{
    const struct watchdog_task_config config = {.name = "early", .timeout_ms = 100};
    zassert_equal(watchdog_task_register(&extra, &config), -EACCES);
    /* 内核启动及进入测试线程不会自行配置或翻转喂狗引脚。 */
    zassert_equal(atomic_get(&feed_configures), 0);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&edges), 0);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    if (IS_ENABLED(CONFIG_TEST_WATCHDOG_BLOCK_FIRST_CHECK)) { block_queue(); }
    if (IS_ENABLED(CONFIG_TEST_WATCHDOG_INIT_FAILURE)) {
        zassert_equal(status_init(), -EIO);
        zassert_ok(watchdog_fault_get(&fault));
        zassert_equal(fault.error, -EIO);
        return NULL;
    }
    zassert_ok(status_init());
    zassert_equal(atomic_get(&feed_configures), IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    if (!IS_ENABLED(CONFIG_TEST_WATCHDOG_BLOCK_FIRST_CHECK)) { status_set_phase(STATUS_RUN); }
    return NULL;
}
ZTEST(task_watchdog, test_registration_and_independent_heartbeats)
{
    const struct watchdog_task_config config = {.name = "sample", .timeout_ms = 100};
    zassert_ok(watchdog_task_register(&first, &config));
    for (int i = 0; i < 5; ++i) {
        k_sleep(K_MSEC(40));
        zassert_ok(watchdog_task_feed(&first));
    }
    struct watchdog_task_snapshot snapshot;
    zassert_ok(watchdog_task_get(&first, &snapshot));
    zassert_equal(snapshot.timeout_ms, 100);
    zassert_true(snapshot.flags & WATCHDOG_TASK_ACTIVE);
    zassert_true(snapshot.last_feed_ms >= 200);
    zassert_equal(atomic_get(&edges) > 0, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
}
ZTEST(task_watchdog, test_one_missing_heartbeat_stops_and_reboots)
{
    const struct watchdog_task_config silent = {.name = "silent", .timeout_ms = 100};
    const struct watchdog_task_config busy = {.name = "busy", .timeout_ms = 200};
    zassert_ok(watchdog_task_register(&first, &silent));
    zassert_ok(watchdog_task_register(&second, &busy));
    expected = &first;
    for (int i = 0; i < 10; ++i) {
        k_sleep(K_MSEC(30));
        zassert_ok(watchdog_task_feed(&second));
    }
    zassert_unreachable("Missing heartbeat did not reboot");
}
ZTEST(task_watchdog, test_zero_registered_continues_past_legacy_timeout)
{
    k_sleep(K_MSEC(1700));
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    status_set_phase(STATUS_RUN);
    zassert_equal(status_phase_get(), STATUS_RUN);
}
ZTEST(task_watchdog, test_registration_validation_without_fixed_capacity)
{
    struct watchdog_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_equal(watchdog_task_feed(&extra), -EACCES);
    zassert_equal(watchdog_task_sleep_forever(&extra), -EACCES);
    struct watchdog_task_snapshot unregistered = {.name = "unchanged", .flags = UINT32_MAX};
    zassert_equal(watchdog_task_get(&extra, &unregistered), -ENOENT);
    zassert_str_equal(unregistered.name, "unchanged");
    zassert_equal(unregistered.flags, UINT32_MAX);
    zassert_equal(watchdog_task_register(NULL, &config), -EINVAL);
    zassert_equal(watchdog_task_register(&extra, NULL), -EINVAL);
    config.name = NULL;
    zassert_equal(watchdog_task_register(&extra, &config), -EINVAL);
    config.name = "";
    zassert_equal(watchdog_task_register(&extra, &config), -EINVAL);
    config.name = "first"; config.timeout_ms = 0;
    zassert_equal(watchdog_task_register(&extra, &config), -EINVAL);
    config.timeout_ms = 2000;
    zassert_ok(watchdog_task_register(&first, &config));
    struct watchdog_task_snapshot before, after;
    zassert_ok(watchdog_task_get(&first, &before));
    k_sleep(K_MSEC(20));
    config.name = "changed"; config.timeout_ms = 100;
    zassert_equal(watchdog_task_register(&first, &config), -EALREADY);
    zassert_ok(watchdog_task_get(&first, &after));
    zassert_str_equal(after.name, "first");
    zassert_equal(after.timeout_ms, 2000);
    zassert_equal(after.last_feed_ms, before.last_feed_ms);
    config.timeout_ms = 2000;
    zassert_ok(watchdog_task_register(&second, &config));
    zassert_ok(watchdog_task_register(&extra, &config));
    static struct watchdog_task additional[16];
    for (size_t i = 0; i < ARRAY_SIZE(additional); ++i) {
        zassert_ok(watchdog_task_register(&additional[i], &config));
        zassert_ok(watchdog_task_feed(&additional[i]));
    }
    for (size_t i = 0; i < ARRAY_SIZE(additional); ++i) {
        zassert_ok(watchdog_task_sleep_forever(&additional[i]));
    }
    zassert_equal(watchdog_task_get(NULL, &after), -EINVAL);
    zassert_equal(watchdog_fault_get(NULL), -EINVAL);
}
ZTEST(task_watchdog, test_explicit_hold_keeps_feed_without_task_fault)
{
    const struct watchdog_task_config config = {.name = "first", .timeout_ms = 2000};
    zassert_ok(watchdog_task_register(&first, &config));
    (void)watchdog_report_fault(-EIO, "Input initialization failed", WATCHDOG_ACTION_HOLD);
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    zassert_equal(watchdog_task_feed(&first), -ECANCELED);
    zassert_equal(watchdog_task_register(&extra, &config), -ECANCELED);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
}
ZTEST(task_watchdog, test_permanent_sleep_and_explicit_reregistration)
{
    struct watchdog_task_config config = {.name = "sample", .timeout_ms = 100};
    zassert_ok(watchdog_task_register(&first, &config));
    k_sleep(K_MSEC(30));
    zassert_ok(watchdog_task_feed(&first));
    struct watchdog_task_snapshot before, sleeping, resumed;
    zassert_ok(watchdog_task_get(&first, &before));
    zassert_ok(watchdog_task_sleep_forever(&first));
    zassert_ok(watchdog_task_sleep_forever(&first));
    zassert_equal(watchdog_task_feed(&first), -EACCES);
    zassert_ok(watchdog_task_get(&first, &sleeping));
    zassert_equal(sleeping.flags, WATCHDOG_TASK_SLEEP_FOREVER);
    zassert_equal(sleeping.last_feed_ms, before.last_feed_ms);
    atomic_val_t edges_before = atomic_get(&edges);
    k_sleep(K_MSEC(250));
    zassert_equal(atomic_get(&edges) > edges_before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    config.name = "resumed"; config.timeout_ms = 200;
    zassert_ok(watchdog_task_register(&first, &config));
    zassert_ok(watchdog_task_get(&first, &resumed));
    zassert_str_equal(resumed.name, "resumed");
    zassert_equal(resumed.timeout_ms, 200);
    zassert_equal(resumed.flags, WATCHDOG_TASK_ACTIVE);
    zassert_true(resumed.last_feed_ms > sleeping.last_feed_ms);
    k_sleep(K_MSEC(120));
    zassert_ok(watchdog_task_feed(&first));
    zassert_ok(watchdog_task_sleep_forever(&first));
}
ZTEST(task_watchdog, test_invalid_resume_preserves_sleep_and_other_tasks)
{
    struct watchdog_task_config config = {.name = "sleeping", .timeout_ms = 100};
    zassert_ok(watchdog_task_register(&first, &config));
    zassert_ok(watchdog_task_sleep_forever(&first));
    config.name = "peer"; config.timeout_ms = 1000;
    zassert_ok(watchdog_task_register(&second, &config));
    zassert_ok(watchdog_task_register(&extra, &config));
    config.name = "resume"; config.timeout_ms = 0;
    zassert_equal(watchdog_task_register(&first, &config), -EINVAL);
    struct watchdog_task_snapshot state;
    zassert_ok(watchdog_task_get(&first, &state));
    zassert_str_equal(state.name, "sleeping");
    zassert_equal(state.timeout_ms, 100);
    zassert_equal(state.flags, WATCHDOG_TASK_SLEEP_FOREVER);
    zassert_equal(watchdog_task_feed(&first), -EACCES);
    zassert_ok(watchdog_task_sleep_forever(&second));
    config.timeout_ms = 300;
    zassert_ok(watchdog_task_register(&first, &config));
    zassert_equal(watchdog_task_feed(&second), -EACCES);
    k_sleep(K_MSEC(180));
    zassert_ok(watchdog_task_feed(&first));
    zassert_ok(watchdog_task_feed(&extra));
    zassert_ok(watchdog_task_sleep_forever(&first));
    zassert_ok(watchdog_task_sleep_forever(&extra));
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(1100));
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
}
ZTEST(task_watchdog, test_resumed_deadline_stops_and_reboots)
{
    struct watchdog_task_config config = {.name = "old", .timeout_ms = 1000};
    zassert_ok(watchdog_task_register(&first, &config));
    zassert_ok(watchdog_task_sleep_forever(&first));
    config.name = "peer";
    zassert_ok(watchdog_task_register(&second, &config));
    config.name = "silent"; config.timeout_ms = 100;
    zassert_ok(watchdog_task_register(&first, &config));
    expected = &first;
    k_sleep(K_MSEC(300));
    zassert_unreachable("Resumed task did not time out");
}
ZTEST(task_watchdog, test_sleep_does_not_protect_peer_stops_and_reboots)
{
    struct watchdog_task_config config = {.name = "sleeping", .timeout_ms = 100};
    zassert_ok(watchdog_task_register(&second, &config));
    zassert_ok(watchdog_task_sleep_forever(&second));
    config.name = "silent"; config.timeout_ms = 150;
    zassert_ok(watchdog_task_register(&first, &config));
    expected = &first;
    k_sleep(K_MSEC(300));
    zassert_unreachable("Peer task did not time out");
}

ZTEST(task_watchdog, test_feeding_earliest_preserves_peer_deadline_stops_and_reboots)
{
    struct watchdog_task_config config = {.name = "fed", .timeout_ms = 80};
    zassert_ok(watchdog_task_register(&first, &config));
    config.name = "silent"; config.timeout_ms = 120;
    zassert_ok(watchdog_task_register(&second, &config));
    expected = &second;
    k_sleep(K_MSEC(60));
    zassert_ok(watchdog_task_feed(&first));
    k_sleep(K_MSEC(200));
    zassert_unreachable("Feeding the earliest task hid its peer deadline");
}

ZTEST(task_watchdog, test_sleeping_earliest_preserves_peer_deadline_stops_and_reboots)
{
    struct watchdog_task_config config = {.name = "sleeping", .timeout_ms = 80};
    zassert_ok(watchdog_task_register(&first, &config));
    config.name = "silent"; config.timeout_ms = 120;
    zassert_ok(watchdog_task_register(&second, &config));
    expected = &second;
    zassert_ok(watchdog_task_sleep_forever(&first));
    k_sleep(K_MSEC(100));
    struct watchdog_task_snapshot snapshot;
    zassert_ok(watchdog_task_get(&second, &snapshot));
    zassert_equal(snapshot.flags, WATCHDOG_TASK_ACTIVE);
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
        const struct watchdog_task_config config = {
            .name = i % 2 ? "slow" : "fast", .timeout_ms = i % 2 ? 600 : 300,
        };
        zassert_ok(watchdog_task_sleep_forever(&first));
        zassert_ok(watchdog_task_register(&first, &config));
        zassert_ok(watchdog_task_feed(&first));
        k_sleep(K_MSEC(1));
    }
    zassert_ok(watchdog_task_sleep_forever(&first));
    k_sem_give(&worker_done);
}
ZTEST(task_watchdog, test_concurrent_snapshots_are_consistent)
{
    const struct watchdog_task_config config = {.name = "fast", .timeout_ms = 300};
    zassert_ok(watchdog_task_register(&first, &config));
    k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                    change_registration, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    int64_t deadline = k_uptime_get() + 1500;
    unsigned snapshots = 0;
    while (k_sem_take(&worker_done, K_NO_WAIT)) {
        struct watchdog_task_snapshot state;
        zassert_ok(watchdog_task_get(&first, &state));
        zassert_true((!strcmp(state.name, "fast") && state.timeout_ms == 300) ||
                     (!strcmp(state.name, "slow") && state.timeout_ms == 600));
        zassert_true(state.flags == WATCHDOG_TASK_ACTIVE || state.flags == WATCHDOG_TASK_SLEEP_FOREVER);
        zassert_true(k_uptime_get() < deadline);
        ++snapshots;
        k_sleep(K_MSEC(1));
    }
    zassert_ok(k_thread_join(&worker, K_MSEC(100)));
    zassert_true(snapshots > 0);
}
ZTEST(task_watchdog, test_configuration_switches)
{
    zassert_equal(watchdog_init(NULL), -EINVAL);
    zassert_equal(atomic_get(&feed_configures), IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    const struct watchdog_task_config config = {.name = "optional", .timeout_ms = 100};
    int err = watchdog_task_register(&first, &config);
    zassert_equal(err, IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE) ? 0 : -EACCES);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&edges) > 0, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    if (!err) { zassert_ok(watchdog_task_sleep_forever(&first)); }
}

/* 系统工作队列被阻塞时，定时检查、关键门控和复位仍须独立执行。 */
K_SEM_DEFINE(queue_entered, 0, 1);
K_SEM_DEFINE(queue_release, 0, 1);
static void block_system_queue(struct k_work *work)
{
    ARG_UNUSED(work);
    atomic_set(&queue_blocked, 1);
    k_sem_give(&queue_entered);
    (void)k_sem_take(&queue_release, K_FOREVER);
    atomic_set(&queue_blocked, 0);
}
static K_WORK_DEFINE(queue_blocker, block_system_queue);

static void block_queue(void)
{
    k_work_submit(&queue_blocker);
    zassert_ok(k_sem_take(&queue_entered, K_SECONDS(1)));
}

ZTEST(task_watchdog, test_check_expiry_with_blocked_workqueue_stops_and_reboots)
{
    block_queue();
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    k_sleep(K_MSEC(3 * CONFIG_WATCHDOG_CHECK_INTERVAL_MS + 30));
    struct watchdog_fault fault;
    if (IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL)) {
        zassert_ok(watchdog_fault_get(&fault));
        zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
        before = atomic_get(&edges);
        k_sleep(K_MSEC(30));
        zassert_equal(atomic_get(&edges), before);
    } else {
        zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    }
    zassert_equal(atomic_get(&protocol_stop_calls), 0);
    expect_check_fault = true;
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(100));
    zassert_unreachable("Expired health was renewed after queue recovery");
}

ZTEST(task_watchdog, test_heartbeats_do_not_renew_health_stops_and_reboots)
{
    block_queue();
    const struct watchdog_task_config config = {.name = "active", .timeout_ms = 10000};
    zassert_ok(watchdog_task_register(&first, &config));
    for (int i = 0; i < 4; ++i) {
        int err = watchdog_task_feed(&first);
        zassert_true(err == 0 || err == -ECANCELED);
        k_sleep(K_MSEC(CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    }
    struct watchdog_fault fault;
    if (IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL)) {
        zassert_ok(watchdog_fault_get(&fault));
        zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
    }
    expect_check_fault = true;
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(100));
    zassert_unreachable("Task heartbeats concealed stalled checks");
}

ZTEST(task_watchdog, test_short_queue_delay_does_not_expire_health)
{
    block_queue();
    k_sleep(K_MSEC(CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(4 * CONFIG_WATCHDOG_CHECK_INTERVAL_MS));
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(task_watchdog, test_explicit_hold_after_expiry_keeps_feed_without_clearing_fault)
{
    block_queue();
    k_sleep(K_MSEC(3 * CONFIG_WATCHDOG_CHECK_INTERVAL_MS + 30));
    if (IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL)) {
        struct watchdog_fault fault;
        zassert_ok(watchdog_fault_get(&fault));
        zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
    }
    (void)watchdog_report_fault(-EIO, "Explicit diagnostic hold", WATCHDOG_ACTION_HOLD);
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(40));
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(atomic_get(&protocol_stop_calls), 1);
}

ZTEST(task_watchdog, test_direct_reset_stops_and_reboots)
{
    block_queue();
    const struct watchdog_task_config config = {.name = "reset", .timeout_ms = 2000};
    zassert_equal(watchdog_task_register(&first, &config),
                  IS_ENABLED(CONFIG_WATCHDOG_SOFTWARE) ? 0 : -EACCES);
    expect_direct_reset = true;
    watchdog_reset();
    zassert_unreachable("Reset returned");
}

#ifdef CONFIG_TEST_WATCHDOG_BLOCK_FIRST_CHECK
ZTEST(task_watchdog, test_startup_grace_expires_before_first_check)
{
    k_sleep(K_MSEC(3 * CONFIG_WATCHDOG_CHECK_INTERVAL_MS + 30));
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
    atomic_val_t before = atomic_get(&edges);
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&edges), before);
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(30));
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
    /* 应用启动 HOLD 显式允许喂狗，但不能把过期记录改为健康。 */
    before = atomic_get(&edges);
    k_sleep(K_MSEC(40));
    zassert_true(atomic_get(&edges) > before);
}
#endif

static atomic_t hold_result;
static void report_hold(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    atomic_set(&hold_result, watchdog_report_fault(-EIO,
               "Injected interrupt hold", WATCHDOG_ACTION_HOLD));
}
static K_TIMER_DEFINE(hold_timer, report_hold, NULL);

ZTEST(task_watchdog, test_interrupt_hold_closes_gate_and_keeps_feed_with_blocked_workqueue)
{
    block_queue();
    const struct watchdog_task_config config = {.name = "held", .timeout_ms = 50};
    zassert_ok(watchdog_task_register(&first, &config));
    atomic_val_t before = atomic_get(&edges);
    k_timer_start(&hold_timer, K_MSEC(10), K_NO_WAIT);
    k_sleep(K_MSEC(150));
    zassert_equal(atomic_get(&hold_result), -EIO);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(atomic_get(&protocol_stop_calls), 1);
    zassert_equal(atomic_get(&last_stop_in_isr), 1);
    zassert_equal(atomic_get(&edges) > before, IS_ENABLED(CONFIG_WATCHDOG_EXTERNAL));
    zassert_equal(watchdog_task_feed(&first), -ECANCELED);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(10));
}

#ifdef CONFIG_TEST_WATCHDOG_INIT_FAILURE
ZTEST(task_watchdog, test_explicit_initialization_failure)
{
    struct status_application_fault fault;
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_ok(status_application_fault_get(&fault));
    zassert_equal(fault.action, WATCHDOG_ACTION_HOLD);
    zassert_equal(fault.error, -EIO);
    k_sleep(K_MSEC(200));
    zassert_equal(atomic_get(&edges), 0);
    struct watchdog_fault watchdog_fault;
    zassert_ok(watchdog_fault_get(&watchdog_fault));
    zassert_equal(watchdog_fault.error, -EIO);
    zassert_equal(status_init(), -EALREADY);
    zassert_equal(atomic_get(&feed_configures), 1);
}
#endif

ZTEST_SUITE(task_watchdog, NULL, setup, NULL, NULL, NULL);
