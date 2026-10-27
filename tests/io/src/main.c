#include <watchdog/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>
#include <zephyr/irq_offload.h>
#include <zephyr/sys/reboot.h>
#include <nsi_cpu_es_if.h>

static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = UINT32_MAX};
static atomic_t toggle_error, configure_error, toggles, configurations, notifications;
static atomic_t query_during_toggle, toggle_queries;
static gpio_flags_t output_flags;
static bool callback_in_isr, expect_reset;
static int64_t reset_requested_ms;
static uint32_t expected_reset_delay_ms = 200;
static unsigned expected_notifications = 1;
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
    if (atomic_get(&query_during_toggle)) {
        struct watchdog_fault fault;
        zassert_true(k_is_in_isr());
        zassert_equal(watchdog_fault_get(&fault), -ENOENT);
        atomic_inc(&toggle_queries);
    }
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
    /* 本夹具只观察回调快照，不改变动作；动作由上报方或 watchdog 内部决定。 */
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
    zassert_equal(fault.reason, WATCHDOG_FAULT_FEED_GPIO);
    zassert_equal(fault.error, -EIO);
    zassert_equal(atomic_get(&notifications), 1);
    zassert_false(callback_in_isr);
    atomic_clear(&toggle_error);
    watchdog_stop(WATCHDOG_STOP_MONITOR); /* HOLD 不能恢复失败的喂狗 GPIO。 */
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&toggles), 0);
    zassert_equal(atomic_get(&notifications), 1);
    zassert_ok(watchdog_report_fault(-EAGAIN, "Later diagnostic", WATCHDOG_ACTION_CONTINUE, 0));
    zassert_ok(watchdog_fault_get(&fault));
    zassert_str_equal(fault.description, "Later diagnostic");
    k_sleep(K_MSEC(30));
    zassert_equal(atomic_get(&toggles), 0); /* 最新记录覆盖不解除监控锁存。 */
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

/* GPIO 驱动内回调诊断查询，验证定时器未持有模块共享锁。 */
ZTEST(io_modules, test_timer_does_not_hold_monitor_lock)
{
    atomic_set(&query_during_toggle, 1);
    zassert_ok(watchdog_init(notify));
    k_sleep(K_MSEC(40));
    watchdog_stop(WATCHDOG_STOP_ALL);
    zassert_true(atomic_get(&toggle_queries) > 0);
    zassert_equal(atomic_get(&toggle_queries), atomic_get(&toggles));
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
    zassert_ok(watchdog_report_fault(-EIO, "ISR diagnostic", WATCHDOG_ACTION_CONTINUE, 0));
}

/* 不链接 status，直接验证通用上报、同步回调和监控诊断互不干扰。 */
ZTEST(io_modules, test_report_is_synchronous_and_preserves_monitoring)
{
    zassert_equal(watchdog_report_fault(-EIO, "Before init", WATCHDOG_ACTION_HOLD, 0), -EACCES);
    zassert_ok(watchdog_init(notify));
    zassert_ok(watchdog_report_fault(-EIO, "Diagnostic", WATCHDOG_ACTION_CONTINUE, 0));
    zassert_equal(atomic_get(&notifications), 1);
    zassert_false(callback_in_isr);
    zassert_equal(reported.reason, WATCHDOG_FAULT_REPORTED);
    zassert_equal(reported.error, -EIO);
    zassert_equal(reported.action, WATCHDOG_ACTION_CONTINUE);
    zassert_equal(strcmp(reported.description, "Diagnostic"), 0);
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_REPORTED);
    zassert_str_equal(fault.description, "Diagnostic");
    atomic_val_t before = atomic_get(&toggles);
    k_sleep(K_MSEC(30));
    zassert_true(atomic_get(&toggles) > before);
    irq_offload(report_from_isr, NULL);
    zassert_true(callback_in_isr);
    zassert_equal(atomic_get(&notifications), 2);
    /* stop 不解除回调绑定，终止后仍可记录新的故障。 */
    watchdog_stop(WATCHDOG_STOP_ALL);
    zassert_equal(watchdog_report_fault(-EIO, "After stop", WATCHDOG_ACTION_HOLD, 0), -EIO);
    zassert_equal(atomic_get(&notifications), 3);
    zassert_equal(watchdog_report_fault(0, "Invalid", WATCHDOG_ACTION_HOLD, 0), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, NULL, WATCHDOG_ACTION_HOLD, 0), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "", WATCHDOG_ACTION_HOLD, 0), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "Invalid", (watchdog_action_t)-1, 0), -EINVAL);
    zassert_equal(atomic_get(&notifications), 3);
}

ZTEST_SUITE(io_modules, NULL, NULL, NULL, NULL, NULL);

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    zassert_true(expect_reset);
    zassert_true(k_is_in_isr());
    zassert_equal(type, SYS_REBOOT_COLD);
    zassert_equal(atomic_get(&notifications), expected_notifications);
    zassert_true(k_uptime_get() - reset_requested_ms >= expected_reset_delay_ms);
    zassert_true(k_uptime_get() - reset_requested_ms < expected_reset_delay_ms + 30);
    printk("GPIO_RESET_PASS\n");
    nsi_exit(0);
    CODE_UNREACHABLE;
}

/* 不链接 status，RESET 仍由 watchdog 在通知后执行。 */
ZTEST(io_modules, test_report_reset_notifies_before_watchdog_resets)
{
    zassert_ok(watchdog_init(notify));
    expect_reset = true;
    expected_reset_delay_ms = 70;
    reset_requested_ms = k_uptime_get();
    zassert_equal(watchdog_report_fault(-EIO, "Reset through watchdog", WATCHDOG_ACTION_RESET, 70), -EIO);
    zassert_equal(reported.reset_delay_ms, 70);
    zassert_equal(atomic_get(&notifications), 1);
    zassert_false(callback_in_isr);
    atomic_val_t before = atomic_get(&toggles);
    k_sleep(K_MSEC(40));
    zassert_true(atomic_get(&toggles) > before);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Report did not schedule a reset");
}

ZTEST(io_modules, test_monitor_fault_resets_without_task_registration)
{
    zassert_ok(watchdog_init(notify));
    expect_reset = true;
    expected_reset_delay_ms = 200;
    reset_requested_ms = k_uptime_get();
    atomic_set(&toggle_error, -EIO);
    k_sleep(K_MSEC(400));
    zassert_unreachable("Monitor fault without registered tasks did not reset");
}

ZTEST(io_modules, test_non_reset_actions_ignore_delay)
{
    zassert_ok(watchdog_init(notify));
    zassert_ok(watchdog_report_fault(-EAGAIN, "Warning", WATCHDOG_ACTION_CONTINUE, UINT32_MAX));
    k_sleep(K_MSEC(40));
    zassert_equal(watchdog_report_fault(-EIO, "Hold", WATCHDOG_ACTION_HOLD, UINT32_MAX), -EIO);
    atomic_val_t before = atomic_get(&toggles);
    k_sleep(K_MSEC(250));
    zassert_true(atomic_get(&toggles) > before);
    zassert_equal(atomic_get(&notifications), 2);
}
K_SEM_DEFINE(queue_entered, 0, 1);
K_SEM_DEFINE(queue_release, 0, 1);
static void block_queue(struct k_work *work)
{
    ARG_UNUSED(work);
    k_sem_give(&queue_entered);
    k_sem_take(&queue_release, K_FOREVER);
}
static K_WORK_DEFINE(queue_blocker, block_queue);

/* 查询不消费通知；HOLD 抑制旧检查故障，新的 GPIO 故障仍可触发复位。 */
ZTEST(io_modules, test_pending_fault_query_and_gpio_upgrade_resets)
{
    zassert_ok(watchdog_init(notify));
    k_work_submit(&queue_blocker);
    zassert_ok(k_sem_take(&queue_entered, K_SECONDS(1)));
    k_sleep(K_MSEC(3 * CONFIG_WATCHDOG_CHECK_INTERVAL_MS + 30));
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_CHECK);
    zassert_equal(atomic_get(&notifications), 0);
    zassert_equal(watchdog_report_fault(-EIO, "Explicit hold", WATCHDOG_ACTION_HOLD, 0), -EIO);
    zassert_ok(watchdog_fault_get(&fault));
    zassert_str_equal(fault.description, "Explicit hold");
    atomic_set(&toggle_error, -EIO);
    k_sleep(K_MSEC(20));
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.reason, WATCHDOG_FAULT_FEED_GPIO);
    zassert_equal(atomic_get(&notifications), 1);
    expect_reset = true;
    expected_notifications = 2;
    reset_requested_ms = k_uptime_get();
    k_sem_give(&queue_release);
    k_sleep(K_MSEC(300));
    zassert_unreachable("GPIO upgrade was lost or a stale CHECK was notified");
}
