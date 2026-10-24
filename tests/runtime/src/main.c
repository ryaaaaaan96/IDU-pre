#include "acquisition/sample.h"
#include "protocol/inc/protocol.h"
#include "protocol/inc/signal_ids.h"
#include "status/status.h"
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <collect/collect.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>
#include "nsi_cpu_es_if.h"

int application_main(void);
static const char *scenario;
static const char *expected_description;
static atomic_t samples, led_edges;
static bool indicator_on;
static k_tid_t application_thread_id;
static struct k_thread application_thread;
static struct status_task monitored;
int __real_flash_area_open(uint8_t id, const struct flash_area **area);
int __wrap_flash_area_open(uint8_t id, const struct flash_area **area)
{
    if (scenario && !strcmp(scenario, "storage-degraded") &&
        id == DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings))) { return -EIO; }
    return __real_flash_area_open(id, area);
}
static atomic_t transmitted_bytes;
static void discard_transmitted(const struct device *device, size_t size, void *user)
{
    ARG_UNUSED(user);
    atomic_add(&transmitted_bytes, size);
    uint8_t bytes[64];
    while (uart_emul_get_tx_data(device, bytes, sizeof(bytes)) > 0) {}
}
K_THREAD_STACK_DEFINE(application_stack, 4096);
DEVICE_DT_DEFINE(DT_NODELABEL(modbus_test), NULL, NULL, NULL, NULL, POST_KERNEL, 90, NULL);

int acquisition_inputs_init(void) { return !strcmp(scenario, "input-init") ? -EIO : 0; }
int acquisition_inputs_sample(struct acquisition_sample *sample)
{
    atomic_inc(&samples);
    *sample = (struct acquisition_sample){.valid = true, .address = 16, .di = 0x1234};
    return !strcmp(scenario, "sample") ? -EIO : 0;
}
void *__real_k_malloc(size_t size);
void *__wrap_k_malloc(size_t size)
{
    if (application_thread_id == k_current_get() && atomic_get(&samples) &&
        !strcmp(scenario, "publish")) { return NULL; }
    return __real_k_malloc(size);
}
static struct gpio_driver_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = BIT_MASK(4)};
static gpio_port_value_t gpio_levels;
static unsigned indicator_writes;

static void record_levels(gpio_port_pins_t mask, gpio_port_value_t value)
{
    gpio_levels = (gpio_levels & ~mask) | (value & mask);
    if (mask & BIT(2)) {
        bool on = !!((gpio_levels ^ gpio_data.invert) & BIT(2));
        if (on != indicator_on) { atomic_inc(&led_edges); }
        indicator_on = on;
    }
}

static int configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(dev);
    if (pin == 2 && scenario && !strcmp(scenario, "led-init")) { return -EIO; }
    record_levels(BIT(pin), (flags & GPIO_OUTPUT_INIT_HIGH) ? BIT(pin) : 0);
    return 0;
}

static int set(const struct device *dev, gpio_port_pins_t mask, gpio_port_value_t value)
{
    ARG_UNUSED(dev);
    if (mask & BIT(2)) {
        ++indicator_writes;
        if (atomic_get(&samples) && !strcmp(scenario, "led")) { return -EIO; }
    }
    record_levels(mask, value);
    return 0;
}
static int set_bits(const struct device *dev, gpio_port_pins_t pins)
{ return set(dev, pins, pins); }
static int clear_bits(const struct device *dev, gpio_port_pins_t pins)
{ return set(dev, pins, 0); }
static int toggle(const struct device *dev, gpio_port_pins_t pins)
{ return set(dev, pins, gpio_levels ^ pins); }
static int get(const struct device *dev, gpio_port_value_t *value)
{ ARG_UNUSED(dev); *value = gpio_levels; return 0; }
static DEVICE_API(gpio, gpio_api) = {
    .pin_configure = configure, .port_set_masked_raw = set,
    .port_set_bits_raw = set_bits, .port_clear_bits_raw = clear_bits,
    .port_toggle_bits = toggle, .port_get_raw = get,
};
DEVICE_DT_DEFINE(DT_NODELABEL(gpio_test), NULL, NULL, &gpio_data, &gpio_config,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &gpio_api);

static bool terminal_result(enum status_action action)
{
    struct status_application_fault fault;
    struct dp_point_state point;
    return !status_application_fault_get(&fault) && fault.description && !strcmp(fault.description, expected_description) &&
        fault.terminal_description && !strcmp(fault.terminal_description, expected_description) && fault.error < 0 && fault.action == action &&
        protocol_stopped() &&
        dp_read((struct dp_key){DEVICE_LOCAL, SIGNAL_DI}, &point) == -ECANCELED;
}
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    bool ok = type == SYS_REBOOT_COLD && terminal_result(STATUS_RESET);
    if (!strcmp(scenario, "led-init")) {
        ok = ok && atomic_get(&samples) == 0 && indicator_writes == 0;
    }
    printk("RUNTIME_RESET_%s\n", ok ? "PASS" : "FAIL");
    nsi_exit(ok ? 0 : 1);
    CODE_UNREACHABLE;
}
static void run_application(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    application_thread_id = k_current_get();
    (void)application_main();
}
ZTEST(runtime, test_real_main_fault_policy)
{
    scenario = getenv("IDU_RUNTIME_CASE");
    if (!scenario) { scenario = "publish"; }
    if (!strcmp(scenario, "publish")) { expected_description = "Sample publication failed"; }
    else if (!strcmp(scenario, "sample")) { expected_description = "Input sampling failed"; }
    else if (!strcmp(scenario, "led")) { expected_description = "Indicator output failed"; }
    else if (!strcmp(scenario, "led-init")) { expected_description = "Indicator initialization failed"; }
    else if (!strcmp(scenario, "input-init")) { expected_description = "Input initialization failed"; }
    else if (!strcmp(scenario, "protocol-init")) { expected_description = "Protocol initialization failed"; }
    else if (!strcmp(scenario, "maintenance")) { expected_description = "Protocol maintenance failed"; }
    else if (!strcmp(scenario, "task-timeout")) { expected_description = "Task heartbeat timed out"; }
    else if (!strcmp(scenario, "force-reset")) { expected_description = "Explicit reset test"; }
    else if (!strcmp(scenario, "storage-degraded")) { expected_description = NULL; }
    else { zassert_unreachable("Unknown scenario"); }
    /* 即使远端断开，UART 仍会在物理线上发送字节。 */
    uart_emul_callback_tx_data_ready_set(DEVICE_DT_GET(DT_NODELABEL(uart_test)),
                                        discard_transmitted, NULL);
    k_thread_create(&application_thread, application_stack, K_THREAD_STACK_SIZEOF(application_stack),
                     run_application, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
    bool maintenance_injected = false;
    struct status_application_fault fault;
    if (!expected_description) {
        k_msleep(1800);
        zassert_true(atomic_get(&samples) >= 3);
        zassert_false(protocol_stopped());
        zassert_equal(status_application_fault_get(&fault), -ENOENT);
        struct dp_param_status storage;
        dp_params_get_status(&storage);
        zassert_true(storage.storage_error < 0);
        struct co_device_status remote;
        zassert_ok(co_device_status(DEVICE_REMOTE_IO, &remote));
        zassert_equal(remote.state, CO_DEVICE_OFFLINE);
        printk("RUNTIME_POLICY_PASS\n");
        return;
    }
    for (int i = 0; i < 300; ++i) {
        if (!strcmp(scenario, "force-reset") && atomic_get(&samples)) {
            (void)status_report_fault(STATUS_RUNTIME, -EIO, "Explicit reset test", STATUS_RESET);
        }
        if (!strcmp(scenario, "maintenance") && atomic_get(&samples) && !maintenance_injected) {
            zassert_ok(modbus_disable(modbus_iface_get_by_name("RAW_1")));
            maintenance_injected = true;
        }
        if (!strcmp(scenario, "task-timeout") && atomic_get(&samples) && !maintenance_injected) {
            const struct status_task_config config = {.name = "test-task", .timeout_ms = 50};
            zassert_ok(status_register(&monitored, &config));
            maintenance_injected = true;
        }
        if (!status_application_fault_get(&fault)) { break; }
        k_msleep(10);
    }
    zassert_ok(status_application_fault_get(&fault));
    if (!fault.terminal_description) {
        zassert_str_equal(expected_description, "Indicator output failed");
        zassert_str_equal(fault.description, expected_description);
        zassert_false(protocol_stopped());
        int before = atomic_get(&samples);
        k_msleep(1100);
        zassert_true(atomic_get(&samples) > before);

        struct dp_point_state point;
        zassert_ok(dp_read((struct dp_key){DEVICE_LOCAL, SIGNAL_DI}, &point));
        zassert_equal(point.quality, DP_QUALITY_GOOD);
    } else {
        zassert_true(terminal_result(STATUS_HOLD), "normal RESET must have exited via reboot");
        k_msleep(150); /* 等待当前操作到达可取消的安全位置。 */
        int before = atomic_get(&samples);
        int sent = atomic_get(&transmitted_bytes);
        const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio_test));
        int previous = gpio_pin_get_raw(gpio, 1), edges = 0;
        for (int i = 0; i < 60; ++i) {
            k_msleep(10);
            int value = gpio_pin_get_raw(gpio, 1);
            edges += value != previous; previous = value;
        }
        zassert_equal(atomic_get(&samples), before);
        zassert_equal(atomic_get(&transmitted_bytes), sent);
        zassert_equal(edges > 0, !IS_ENABLED(CONFIG_APP_DEBUG_SKIP_WATCHDOG));
        if (strcmp(scenario, "led") && strcmp(scenario, "led-init")) { zassert_true(indicator_on); }
    }
    printk("RUNTIME_POLICY_PASS\n");
}
ZTEST_SUITE(runtime, NULL, NULL, NULL, NULL, NULL);
