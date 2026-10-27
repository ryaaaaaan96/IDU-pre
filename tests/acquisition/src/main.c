#include <dataprocess/acquisition.h>
#include "protocol/inc/protocol.h"
#include <transmit/forward.h>
#include "acquisition/acquisition.h"
#include "status/inc/status.h"
#include "status/status_led.h"
#include "process/points.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <platform_callback.h>
#include <iap/iap.h>
#include <zephyr/drivers/gpio.h>
#include <watchdog/watchdog.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/dfu/mcuboot.h>
#include "nsi_cpu_es_if.h"

/* main.c 将 startup 保留在应用入口内，不提供公共头文件。 */
int startup(void);
int application_cycle(void);

typedef enum { WATCHDOG_SERVICE_INIT, LED_START, INPUTS_INIT, IAP_INIT, TABLES_INIT, DP_INIT, DATA_BIND, TASKS_PREPARE,
               NETWORK_INIT, PORT_OPEN, COLLECT_INIT, PARAMS_INIT, PARAMS_RECOVER,
               PROCESS_INIT, REFERENCE_INIT, COLLECT_START, FORWARD_INIT, IMAGE_CONFIRM,
               SAMPLE, PUBLISH, PROTOCOL, LED, SERVICES } service_t;
static int errors[SERVICES];
static unsigned calls[SERVICES];
static bool healthy, boot_ready;
static unsigned dp_health_checks;
static int led_set_error;
static bool indicator_on;
static unsigned indicator_edges, sample_delay_ms;
static bool protocol_halted;
void protocol_stop(void) { protocol_halted = true; }
bool protocol_stopped(void) { return protocol_halted; }
static bool published;
static int previous_startup_service;
static uint16_t parameter_flags;
static int parameter_storage_error;
static unsigned workers_started;
static struct dp_device_cfg remote_device[] = {{.dev_id = 10, .unit_id = 1}};
static const struct dp_equip_cfg point_config = {.devices = remote_device, .device_count = 1};
static co_device_online_t remote_state;
/* 编排测试夹具提供与真实 CO 相同的事件契约。 */
ZBUS_CHAN_DEFINE(co_device_events, struct co_device_status, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
const struct co_bus_cfg protocol_collect_bus = {0};
const struct co_port_config protocol_collect_port;

static int service(service_t service)
{
    static const status_phase_t phases[SERVICES] = {
        [WATCHDOG_SERVICE_INIT] = STATUS_STARTING,
        [LED_START] = STATUS_STARTING,
        [INPUTS_INIT] = STATUS_STARTING,
        [TABLES_INIT] = STATUS_STARTING,
        [DP_INIT] = STATUS_STARTING,
        [DATA_BIND] = STATUS_STARTING,
        [TASKS_PREPARE] = STATUS_STARTING,
        [NETWORK_INIT] = STATUS_STARTING,
        [PORT_OPEN] = STATUS_STARTING,
        [COLLECT_INIT] = STATUS_STARTING,
        [PARAMS_INIT] = STATUS_STARTING,
        [PARAMS_RECOVER] = STATUS_STARTING,
        [PROCESS_INIT] = STATUS_STARTING,
        [REFERENCE_INIT] = STATUS_STARTING,
        [COLLECT_START] = STATUS_STARTING,
        [IAP_INIT] = STATUS_STARTING,
        [FORWARD_INIT] = STATUS_STARTING,
        [SAMPLE] = STATUS_RUN,
        [PUBLISH] = STATUS_RUN,
        [PROTOCOL] = STATUS_RUN,
        [LED] = STATUS_RUN,
        [IMAGE_CONFIRM] = STATUS_STARTING,
    };
    zassert_equal(status_phase_get(), phases[service]);
    if (service <= IMAGE_CONFIRM) {
        zassert_true((int)service > previous_startup_service, "Startup order changed");
        previous_startup_service = service;
    }
    ++calls[service];
    return errors[service];
}

int __real_watchdog_init(watchdog_fault_cb_t notify);
int __wrap_watchdog_init(watchdog_fault_cb_t notify)
{
    zassert_ok(__real_watchdog_init(notify));
    return service(WATCHDOG_SERVICE_INIT);
}
int acquisition_inputs_init(void) { return service(INPUTS_INIT); }
int protocol_register_point_tables(void) { return service(TABLES_INIT); }
int protocol_dp_tasks_init(void) { return service(TASKS_PREPARE); }
int protocol_co_tasks_init(void) { return 0; }
void protocol_dp_tasks_start(void) { workers_started |= 1; }
void protocol_co_tasks_start(void) { workers_started |= 2; }
int co_set_data_callbacks(const struct co_data_callbacks *callbacks)
{
    zassert_equal_ptr(callbacks->sample, dp_accept_samples);
    return service(DATA_BIND);
}
int dp_accept_samples(struct dp_point_batch *batch, bool sync) { return 0; }
int co_device_status(uint16_t dev_id, struct co_device_status *status)
{
    zassert_equal(dev_id, 10);
    *status = (struct co_device_status){.dev_id = dev_id, .state = remote_state};
    return 0;
}
int dp_initialize_acquired_device(uint16_t dev_id) { return 0; }
int dp_invalidate_acquired_device(uint16_t dev_id, int error, int64_t observed_at) { return 0; }
int32_t co_dp_fast_set(PARA_DATA_S *data, PARA_SET_STATUS status) { return 0; }
int32_t dp_para_reg_set_func(PARA_SET_FUNC callback) { return 0; }
int dp_init(void) { return service(DP_INIT); }
const struct dp_equip_cfg *dp_config(void) { return &point_config; }
int protocol_ports_init(void)
{
    return service(NETWORK_INIT);
}
int co_port_open(const struct co_port_config *config)
{
    zassert_equal_ptr(config, &protocol_collect_port);
    int err = service(PORT_OPEN);
    return err ? err : 3;
}
int co_init_bus(const struct dp_equip_cfg *config, const struct co_bus_cfg *bus)
{
    zassert_equal_ptr(config, &point_config);
    zassert_equal_ptr(bus, &protocol_collect_bus);
    return service(COLLECT_INIT);
}
int co_set_device_policy(uint16_t dev, const struct co_device_policy *policy) { return 0; }
int dp_params_init(const struct dp_equip_cfg *config, uint8_t area_id)
{
    zassert_equal_ptr(config, &point_config);
    zassert_equal(area_id, DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)));
    parameter_storage_error = service(PARAMS_INIT);
    return parameter_storage_error;
}
int dp_params_recover(void) { return service(PARAMS_RECOVER); }
int dp_process_init(const struct dp_process_plan *plan)
{
    zassert_true(plan->initialize_all);
    return service(PROCESS_INIT);
}
int dp_reference_init(void) { return service(REFERENCE_INIT); }
int co_polling_init(void) { return service(COLLECT_START); }
uint16_t points_sample_period_ms(void) { return 500; }
int iap_init(void) { return service(IAP_INIT); }
int protocol_forwarding_init(void) { return service(FORWARD_INIT); }
void iap_stop(void) {}

static const char *expected_description;
static void (*reset_check)(void);

/* RESET 是终止动作：各用例在复位钩子内校验最终状态，然后结束进程。 */
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    zassert_equal(type, SYS_REBOOT_COLD);
    if (reset_check) { reset_check(); }
    printk("ACQUISITION_RESET_PASS\n");
    nsi_exit(0);
    CODE_UNREACHABLE;
}

static struct gpio_driver_data indicator_data;
static const struct gpio_driver_config indicator_config = {.port_pin_mask = UINT32_MAX};
static bool indicator_level, fault_on_write;
static unsigned indicator_writes;

static void record_level(bool high)
{
    bool on = high ^ !!(indicator_data.invert & BIT(0));
    indicator_edges += indicator_on != on;
    indicator_on = on;
    indicator_level = high;
}

static int configure_indicator(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
    ARG_UNUSED(dev); zassert_equal(pin, 0);
    int err = service(LED_START);
    if (!err) { err = led_set_error; }
    if (!err) { record_level(flags & GPIO_OUTPUT_INIT_HIGH); }
    return err;
}

static int write_indicator(bool high)
{
    ++indicator_writes;
    int err = status_phase_get() == STATUS_RUN ? service(LED) : 0;
    if (!err) { err = led_set_error; }
    if (!err) { record_level(high); }
    if (fault_on_write) {
        fault_on_write = false;
        (void)watchdog_report_fault(-EIO, "Injected concurrent fault", WATCHDOG_ACTION_HOLD, 0);
    }
    return err;
}

static int set_indicator(const struct device *dev, gpio_port_pins_t pins)
{ ARG_UNUSED(dev); zassert_equal(pins, BIT(0)); return write_indicator(true); }
static int clear_indicator(const struct device *dev, gpio_port_pins_t pins)
{ ARG_UNUSED(dev); zassert_equal(pins, BIT(0)); return write_indicator(false); }
static int toggle_indicator(const struct device *dev, gpio_port_pins_t pins)
{
    ARG_UNUSED(dev); zassert_equal(pins, BIT(0));
    return write_indicator(!indicator_level);
}

static int indicator_device_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    const char *test = getenv("IDU_ACQUISITION_CASE");
    return test && strstr(test, "unready_indicator") ? -ENODEV : 0;
}

static DEVICE_API(gpio, indicator_api) = {
    .pin_configure = configure_indicator,
    .port_set_bits_raw = set_indicator,
    .port_clear_bits_raw = clear_indicator,
    .port_toggle_bits = toggle_indicator,
};
DEVICE_DT_DEFINE(DT_NODELABEL(gpio0), indicator_device_init, NULL,
                 &indicator_data, &indicator_config,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &indicator_api);
DEVICE_DT_DEFINE(DT_NODELABEL(external_flash), NULL, NULL, NULL, NULL,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
void dp_params_get_status(struct dp_param_status *status)
{
    *status = (struct dp_param_status){
        .flags = boot_ready ? parameter_flags : 0,
        .error = errors[PARAMS_INIT],
        .storage_error = (parameter_flags & DP_PARAM_STORAGE_READY) ? 0 : parameter_storage_error,
    };
}
bool dp_healthy(uint32_t max_idle_ms)
{
    ++dp_health_checks;
    ARG_UNUSED(max_idle_ms);
    return healthy;
}

int acquisition_cycle(void)
{
    if (sample_delay_ms) { k_msleep(sample_delay_ms); }
    int err = service(SAMPLE);
    if (err) { return err; }
    published = true;
    return service(PUBLISH);
}

const struct tr_forward_cfg protocol_forwarding;

int protocol_error(void)
{
    return service(PROTOCOL);
}

int boot_write_img_confirmed(void)
{
    return service(IMAGE_CONFIRM);
}

static void initialize_fixture(void)
{
    memset(errors, 0, sizeof(errors));
    memset(calls, 0, sizeof(calls));
    published = false;
    healthy = boot_ready = true;
    dp_health_checks = 0;
    led_set_error = 0;
    indicator_on = false;
    indicator_edges = sample_delay_ms = 0;
    protocol_halted = false;
    previous_startup_service = -1;
    parameter_flags = DP_PARAM_STORAGE_READY;
    parameter_storage_error = 0;
    workers_started = 0;
    remote_state = CO_DEVICE_ONLINE;
    remote_device[0].unit_id = 1;
    reset_check = NULL;
    expected_description = NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    static bool initialized;
    zassert_false(initialized, "Run cases in separate processes with tests/acquisition/run.py");
    initialized = true;
    initialize_fixture();
}

static struct watchdog_fault fault_snapshot(void)
{
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    return fault;
}

/* 复位钩子在 watchdog 定时器中断中执行，只校验终止状态。 */
static void check_runtime_reset(void)
{
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_true(protocol_halted);
    if (expected_description) {
        zassert_str_equal(fault_snapshot().description, expected_description);
    }
}

static void check_startup_failure(service_t failed)
{
    errors[failed] = -EIO;
    zassert_equal(startup(), -EIO);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(fault_snapshot().error, -EIO);
    const char *description = "Protocol initialization failed";
    if (failed == WATCHDOG_SERVICE_INIT) { description = "Watchdog initialization failed"; }
    else if (failed == LED_START) { description = "Indicator initialization failed"; }
    else if (failed == INPUTS_INIT) { description = "Input initialization failed"; }
    else if (failed == IAP_INIT) { description = "Upgrade service initialization failed"; }
    else if (failed == IMAGE_CONFIRM) { description = "Image confirmation failed"; }
    zassert_str_equal(fault_snapshot().description, description);
    for (service_t s = WATCHDOG_SERVICE_INIT; s <= IMAGE_CONFIRM; ++s) {
        zassert_equal(calls[s], s <= failed ? 1 : 0);
    }
    zassert_true(protocol_halted);
    zassert_equal(fault_snapshot().action, WATCHDOG_ACTION_HOLD);
    zassert_equal(calls[IMAGE_CONFIRM], failed == IMAGE_CONFIRM ? 1 : 0);
    zassert_equal(application_cycle(), -EACCES);
    zassert_equal(startup(), -EALREADY);
    zassert_equal(fault_snapshot().error, -EIO);
}

ZTEST(acquisition, test_startup_failure_watchdog_init)
{
    check_startup_failure(WATCHDOG_SERVICE_INIT);
    zassert_equal(calls[IMAGE_CONFIRM], 0);
}

ZTEST(acquisition, test_startup_failure_led_start)
{
    check_startup_failure(LED_START);
}

ZTEST(acquisition, test_startup_failure_inputs_init)
{
    check_startup_failure(INPUTS_INIT);
}

ZTEST(acquisition, test_startup_failure_iap_init)
{
    check_startup_failure(IAP_INIT);
}

ZTEST(acquisition, test_startup_failure_tables_init)
{
    check_startup_failure(TABLES_INIT);
}

ZTEST(acquisition, test_startup_failure_dp_init)
{
    check_startup_failure(DP_INIT);
}

ZTEST(acquisition, test_startup_failure_data_bind)
{
    check_startup_failure(DATA_BIND);
}

ZTEST(acquisition, test_startup_failure_tasks_prepare)
{
    check_startup_failure(TASKS_PREPARE);
}

ZTEST(acquisition, test_startup_failure_network_init)
{
    check_startup_failure(NETWORK_INIT);
}

ZTEST(acquisition, test_startup_failure_port_open)
{
    check_startup_failure(PORT_OPEN);
}

ZTEST(acquisition, test_startup_failure_collect_init)
{
    check_startup_failure(COLLECT_INIT);
}

ZTEST(acquisition, test_startup_failure_process_init)
{
    check_startup_failure(PROCESS_INIT);
}

ZTEST(acquisition, test_startup_failure_reference_init)
{
    check_startup_failure(REFERENCE_INIT);
}

ZTEST(acquisition, test_startup_failure_collect_start)
{
    check_startup_failure(COLLECT_START);
}

ZTEST(acquisition, test_startup_failure_forward_init)
{
    check_startup_failure(FORWARD_INIT);
}

ZTEST(acquisition, test_failed_forwarding_never_starts_acquisition)
{
    errors[FORWARD_INIT] = -EIO;
    zassert_equal(startup(), -EIO);
    zassert_equal(workers_started, 0,
                  "Do not release acquisition before local protocol assembly completes");
}

ZTEST(acquisition, test_success_confirms_at_startup_and_updates_points)
{
    zassert_ok(startup());
    zassert_equal(status_phase_get(), STATUS_RUN);
    zassert_equal(workers_started, 3);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    zassert_ok(application_cycle());
    zassert_equal(status_phase_get(), STATUS_RUN);
    zassert_true(published);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    boot_ready = false;
    k_sleep(K_MSEC(1600)); /* 采样间隔不再影响已经完成的确认。 */
    zassert_ok(application_cycle());
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(acquisition, test_failed_acquisition_stops_before_maintenance)
{
    zassert_ok(startup());
    errors[SAMPLE] = -ENODEV;
    errors[PUBLISH] = -ENOMEM;
    expected_description = "Input point update failed";
    reset_check = check_runtime_reset;
    zassert_equal(application_cycle(), -ENODEV);
    zassert_equal(calls[PUBLISH], 0);
    zassert_false(published);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Runtime fault did not reach the reboot hook");
}

static void check_cycle_failure(service_t failed, const char *description)
{
    zassert_ok(startup());
    errors[failed] = -EIO;
    expected_description = description;
    reset_check = check_runtime_reset;
    zassert_equal(application_cycle(), -EIO);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Runtime fault did not reach the reboot hook");
}

ZTEST(acquisition, test_point_write_failure_is_terminal)
{
    check_cycle_failure(PUBLISH, "Input point update failed");
}

ZTEST(acquisition, test_protocol_failure_is_terminal)
{
    check_cycle_failure(PROTOCOL, "Protocol maintenance failed");
}

ZTEST(acquisition, test_unregistered_dp_has_no_implicit_deadline)
{
    zassert_ok(startup());
    healthy = false;
    zassert_ok(application_cycle());
    zassert_equal(dp_health_checks, 0);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(acquisition, test_confirmation_failure_holds_startup_without_sampling)
{
    check_startup_failure(IMAGE_CONFIRM);
    zassert_equal(calls[SAMPLE], 0);
}

ZTEST(acquisition, test_gpio_watchdog_error_stops_runtime_after_confirmation)
{
    zassert_ok(startup());
    expected_description = "Watchdog feed output failed";
    reset_check = check_runtime_reset;
    (void)watchdog_report_fault(-EIO, "Watchdog feed output failed", WATCHDOG_ACTION_RESET, 200);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Watchdog fault did not reach the reboot hook");
}

ZTEST(acquisition, test_lifecycle_guards)
{
    zassert_equal(status_phase_get(), STATUS_UNCONFIGURED);
    zassert_equal(watchdog_report_fault(-EIO, "Input point update failed", WATCHDOG_ACTION_RESET, 200), -EACCES);
    zassert_equal(application_cycle(), -EACCES);
    zassert_equal(calls[SAMPLE], 0);
    zassert_ok(startup());
    zassert_equal(startup(), -EALREADY);
    zassert_equal(status_init(), -EALREADY);
    zassert_equal(status_phase_get(), STATUS_RUN);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    zassert_equal(calls[TABLES_INIT], 1);
    zassert_equal(calls[WATCHDOG_SERVICE_INIT], 1);
    zassert_equal(calls[LED_START], 1);
}

ZTEST(acquisition, test_startup_cannot_continue_after_fault)
{
    zassert_ok(status_init());
    zassert_equal(watchdog_report_fault(-EIO, "Initialization cannot continue",
                                       WATCHDOG_ACTION_CONTINUE, 0), -EINVAL);
    zassert_equal(status_phase_get(), STATUS_STARTING);
    struct watchdog_fault fault;
    zassert_ok(watchdog_fault_get(&fault));
    zassert_equal(fault.action, WATCHDOG_ACTION_CONTINUE);
}

ZTEST(acquisition, test_fault_arguments_do_not_change_state)
{
    zassert_ok(startup());
    zassert_equal(watchdog_report_fault(0, "Invalid error code", WATCHDOG_ACTION_RESET, 200), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, NULL, WATCHDOG_ACTION_RESET, 200), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "", WATCHDOG_ACTION_RESET, 200), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "Invalid action", (watchdog_action_t)-1, 0), -EINVAL);
    zassert_equal(watchdog_report_fault(-EIO, "Invalid action", (watchdog_action_t)3, 0), -EINVAL);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(acquisition, test_explicit_fault_actions_record_latest_error)
{
    zassert_ok(startup());
    static const char first[] = "Auxiliary output error";
    zassert_ok(watchdog_report_fault(-EIO, first, WATCHDOG_ACTION_CONTINUE, 0));
    struct watchdog_fault fault = fault_snapshot();
    zassert_equal_ptr(fault.description, first);
    zassert_equal(fault.action, WATCHDOG_ACTION_CONTINUE);
    zassert_equal(status_phase_get(), STATUS_RUN);
    zassert_false(protocol_halted);

    zassert_equal(watchdog_report_fault(-ETIMEDOUT, "Input unavailable", WATCHDOG_ACTION_HOLD, 0), -ETIMEDOUT);
    fault = fault_snapshot();
    zassert_str_equal(fault.description, "Input unavailable");
    zassert_equal(fault.error, -ETIMEDOUT);
    zassert_equal(fault.action, WATCHDOG_ACTION_HOLD);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_true(protocol_halted);

    zassert_equal(watchdog_report_fault(-ECANCELED, "Subsequent cancellation", WATCHDOG_ACTION_HOLD, 0), -ECANCELED);
    zassert_str_equal(fault_snapshot().description, "Subsequent cancellation");
}

ZTEST(acquisition, test_reset_request_is_terminal)
{
    zassert_ok(startup());
    expected_description = "Reset requested";
    reset_check = check_runtime_reset;
    (void)watchdog_report_fault(-EIO, "Reset requested", WATCHDOG_ACTION_RESET, 200);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Reset request did not reach the reboot hook");
}

ZTEST(acquisition, test_status_records_latest_fault_without_resuming_business)
{
    zassert_ok(startup());
    zassert_true(indicator_on);
    led_set_error = -ENODEV; /* 故障指示本身失败不能覆盖采样错误。 */
    errors[SAMPLE] = -EIO;
    expected_description = "Input point update failed";
    reset_check = check_runtime_reset;
    zassert_equal(application_cycle(), -EIO);
    k_sleep(K_MSEC(300));
    zassert_unreachable("Sample fault did not reach the reboot hook");
}

ZTEST(acquisition, test_led_tracks_startup_healthy_cycles_and_fault)
{
    zassert_ok(startup());
    zassert_true(indicator_on);
    zassert_false(indicator_level); /* 设备树指定低电平点亮。 */
    zassert_ok(application_cycle());
    zassert_true(indicator_on); /* 采样不再决定闪烁速度。 */
    k_sleep(K_MSEC(130));
    zassert_false(indicator_on);
    zassert_true(indicator_level);
    zassert_ok(application_cycle());
    zassert_false(indicator_on);
    k_sleep(K_MSEC(130));
    zassert_true(indicator_on);
    (void)watchdog_report_fault(-EIO, "Input point update failed", WATCHDOG_ACTION_HOLD, 0);
    zassert_true(indicator_on);
    k_sleep(K_MSEC(100));
    zassert_true(indicator_on);
    zassert_true(protocol_halted);
}

ZTEST(acquisition, test_led_selects_pattern_by_phase)
{
    zassert_ok(status_init());
    zassert_true(indicator_on);
    k_msleep(550);
    zassert_false(indicator_on); /* 启动阶段慢闪。 */

    status_set_phase(STATUS_RUN);
    k_msleep(10);
    zassert_true(indicator_on);
    k_msleep(130);
    zassert_false(indicator_on); /* 运行阶段快闪。 */

    (void)watchdog_report_fault(-EIO, "Input point update failed", WATCHDOG_ACTION_HOLD, 0);
    k_msleep(10);
    zassert_true(indicator_on); /* 终止故障常亮。 */
    unsigned fault_edges = indicator_edges;
    k_msleep(230);
    zassert_true(indicator_on);
    zassert_equal(indicator_edges, fault_edges);
}

ZTEST(acquisition, test_indicator_progresses_during_blocking_sampling)
{
    zassert_ok(startup());
    unsigned before = indicator_edges;
    sample_delay_ms = 350;
    zassert_ok(application_cycle());
    zassert_true(indicator_edges >= before + 3);
}

K_SEM_DEFINE(indicator_queue_entered, 0, 1);
K_SEM_DEFINE(indicator_queue_release, 0, 1);
static void delay_indicator_queue(struct k_work *work)
{
    ARG_UNUSED(work);
    k_sem_give(&indicator_queue_entered);
    (void)k_sem_take(&indicator_queue_release, K_FOREVER);
}
static K_WORK_DEFINE(indicator_queue_blocker, delay_indicator_queue);

ZTEST(acquisition, test_late_indicator_advances_one_step_and_fault_takes_priority)
{
    zassert_ok(startup());
    unsigned before = indicator_edges;
    k_work_submit(&indicator_queue_blocker);
    zassert_ok(k_sem_take(&indicator_queue_entered, K_SECONDS(1)));
    k_msleep(350);
    unsigned while_delayed = indicator_edges;
    k_sem_give(&indicator_queue_release);
    k_msleep(10);
    zassert_equal(while_delayed, before);
    zassert_equal(indicator_edges, before + 1); /* 恢复后只推进一步，每步保持完整时长。 */
    zassert_false(indicator_on);
    (void)watchdog_report_fault(-EIO, "Input point update failed", WATCHDOG_ACTION_HOLD, 0);
    k_msleep(10);
    zassert_true(indicator_on);
    unsigned fault_edges = indicator_edges;
    k_msleep(250);
    zassert_true(indicator_on);
    zassert_equal(indicator_edges, fault_edges);
}

ZTEST(acquisition, test_fault_during_gpio_write_forces_indicator_on)
{
    zassert_ok(startup());
    fault_on_write = true;
    k_msleep(110);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_true(indicator_on);
    zassert_equal(fault_snapshot().action, WATCHDOG_ACTION_HOLD);
}

ZTEST(acquisition, test_late_indicator_does_not_skip_even_periods)
{
    zassert_ok(startup());
    unsigned before = indicator_edges;
    k_work_submit(&indicator_queue_blocker);
    zassert_ok(k_sem_take(&indicator_queue_entered, K_SECONDS(1)));
    k_msleep(250);
    k_sem_give(&indicator_queue_release);
    k_msleep(10);
    zassert_false(indicator_on);
    zassert_equal(indicator_edges, before + 1);
}

ZTEST(acquisition, test_unready_indicator_stops_startup_without_gpio_access)
{
    zassert_equal(startup(), -ENODEV);
    zassert_equal(calls[LED_START], 0);
    zassert_equal(calls[INPUTS_INIT], 0);
    zassert_str_equal(fault_snapshot().description, "Indicator initialization failed");
    k_msleep(250);
    zassert_equal(indicator_writes, 0);
}

ZTEST(acquisition, test_initial_led_output_failure_stops_startup)
{
    led_set_error = -EIO;
    zassert_equal(startup(), -EIO);
    zassert_equal(calls[INPUTS_INIT], 0);
    zassert_equal(fault_snapshot().action, WATCHDOG_ACTION_HOLD);
}

ZTEST(acquisition, test_storage_degradation_does_not_block_startup_confirmation)
{
    errors[PARAMS_INIT] = -EIO;
    parameter_flags = 0;
    zassert_ok(startup());
    zassert_equal(calls[PARAMS_RECOVER], 0);
    zassert_equal(calls[PROCESS_INIT], 1);
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(application_cycle());
    zassert_equal(parameter_storage_error, -EIO);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
}

ZTEST(acquisition, test_pending_recovery_does_not_delay_startup_confirmation)
{
    errors[PARAMS_RECOVER] = -ETIMEDOUT;
    parameter_flags = DP_PARAM_STORAGE_READY | DP_PARAM_PENDING;
    zassert_ok(startup());
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(application_cycle());

    parameter_flags = DP_PARAM_STORAGE_READY;
    zassert_ok(application_cycle());
    zassert_equal(calls[IMAGE_CONFIRM], 1);
}

ZTEST(acquisition, test_remote_outage_is_diagnostic_and_keeps_watchdog_progress)
{
    zassert_ok(startup());
    remote_state = CO_DEVICE_OFFLINE;
    zassert_ok(application_cycle());
    struct co_device_status remote;
    zassert_ok(co_device_status(10, &remote));
    zassert_equal(remote.state, CO_DEVICE_OFFLINE);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
    remote_state = CO_DEVICE_INITIALIZING;
    zassert_ok(application_cycle());
    zassert_ok(co_device_status(10, &remote));
    zassert_equal(remote.state, CO_DEVICE_INITIALIZING);
    zassert_equal(calls[IMAGE_CONFIRM], 1);
}

ZTEST(acquisition, test_shared_wire_station_conflict_fails_before_ports_or_workers)
{
    remote_device[0].unit_id = CONFIG_IDU_FORWARD_UNIT;
    zassert_equal(startup(), -EADDRINUSE);
    zassert_equal(calls[NETWORK_INIT], 0);
    zassert_equal(calls[TASKS_PREPARE], 0);
    zassert_equal(calls[FORWARD_INIT], 0);
    zassert_equal(workers_started, 0);
}

ZTEST(acquisition, test_storage_diagnostics_are_read_from_dataprocess)
{
    errors[PARAMS_INIT] = -EIO;
    parameter_flags = 0;
    zassert_ok(startup());
    struct dp_param_status storage;
    dp_params_get_status(&storage);
    zassert_equal(storage.storage_error, -EIO);
    zassert_ok(application_cycle());
    parameter_storage_error = 0;
    parameter_flags = DP_PARAM_STORAGE_READY;
    dp_params_get_status(&storage);
    zassert_equal(storage.storage_error, 0);
    zassert_true(storage.flags & DP_PARAM_STORAGE_READY);
    struct watchdog_fault fault;
    zassert_equal(watchdog_fault_get(&fault), -ENOENT);
}

ZTEST_SUITE(acquisition, NULL, NULL, before, NULL, NULL);

/* 重复状态继续原循环，故障状态保持常亮。 */
ZTEST(acquisition, test_same_indicator_state_does_not_restart_and_fault_holds)
{
    zassert_ok(status_init());
    status_led_set(STATUS_LED_RUN);
    k_msleep(60);
    status_led_set(STATUS_LED_RUN);
    k_msleep(60);
    zassert_false(indicator_on);
    k_msleep(100);
    zassert_true(indicator_on); /* 循环回到第一步。 */
    status_led_set(STATUS_LED_FAULT);
    k_msleep(10);
    unsigned before = indicator_edges;
    k_msleep(250);
    zassert_true(indicator_on);
    zassert_equal(indicator_edges, before);
}
