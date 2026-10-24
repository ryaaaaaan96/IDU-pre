#include <dataprocess/acquisition.h>
#include "protocol/inc/protocol.h"
#include <transmit/forward.h>
#include "acquisition/acquisition.h"
#include "process/points.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <platform_callback.h>
#include <acquisition/sample.h>
#include <iap/iap.h>
#include <zephyr/drivers/led.h>
#include <watchdog/watchdog.h>
#include "status/task_watchdog/internal.h"
#include <zephyr/ztest.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>
#include "status/image_confirm/image_confirm.h"

/* main.c keeps startup local to the application entry, without a public header. */
int startup(void);

enum service { WATCHDOG_INIT, LED_START, INPUTS_INIT, IAP_INIT, TABLES_INIT, DP_INIT, DATA_BIND, TASKS_PREPARE,
               NETWORK_INIT, PORT_OPEN, COLLECT_INIT, PARAMS_INIT, PARAMS_RECOVER,
               PROCESS_INIT, REFERENCE_INIT, COLLECT_START, FORWARD_INIT,
               SAMPLE, PUBLISH, PROTOCOL, LED, CHECKPOINT, WATCHDOG_CHECK, SERVICES };
static int errors[SERVICES];
static unsigned calls[SERVICES], checks, stops;
static bool healthy, boot_ready, checkpoint_ready;
static unsigned dp_health_checks;
static int led_set_error;
static bool indicator_on;
static unsigned indicator_edges, sample_delay_ms;
static bool protocol_halted;
void protocol_stop(void) { protocol_halted = true; }
bool protocol_stopped(void) { return protocol_halted; }
static struct acquisition_sample published;
static int previous_startup_service;
static uint16_t parameter_flags;
static int parameter_storage_error;
static unsigned workers_started;
static struct dp_device_cfg remote_device[] = {{.dev_id = 10, .unit_id = 1}};
static const struct dp_equip_cfg point_config = {.devices = remote_device, .device_count = 1};
static enum co_device_online remote_state;
/* The orchestration fixture supplies the same event contract as real CO. */
ZBUS_CHAN_DEFINE(co_device_events, struct co_device_status, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
const struct co_bus_cfg protocol_collect_bus = {0};
const struct co_port_config protocol_collect_port;

static int service(enum service service)
{
    static const enum status_phase phases[SERVICES] = {
        [WATCHDOG_INIT] = STATUS_STARTING,
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
        [SAMPLE] = STATUS_SAMPLING,
        [PUBLISH] = STATUS_PUBLISHING,
        [PROTOCOL] = STATUS_SERVICING_COMMUNICATION,
        [LED] = STATUS_CHECKING_HEALTH,
        [CHECKPOINT] = STATUS_CHECKING_HEALTH,
        [WATCHDOG_CHECK] = STATUS_CHECKING_HEALTH,
    };
    zassert_equal(status_phase_get(), phases[service]);
    if (service <= FORWARD_INIT) {
        zassert_true((int)service > previous_startup_service, "Startup order changed");
        previous_startup_service = service;
    }
    ++calls[service];
    return errors[service];
}

int watchdog_init(struct watchdog *monitor, const struct watchdog_config *config)
{ return service(WATCHDOG_INIT); }
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
int watchdog_error(struct watchdog *monitor) { ++checks; ++calls[WATCHDOG_CHECK]; return errors[WATCHDOG_CHECK]; }
void watchdog_stop(struct watchdog *monitor) { ++stops; }
int status_tasks_init(struct watchdog *monitor, void (*expired)(void))
{ ARG_UNUSED(monitor); ARG_UNUSED(expired); return 0; }
void status_tasks_halt(struct watchdog *monitor, bool stop_feeding)
{ if (stop_feeding) { watchdog_stop(monitor); } }
void status_request_reboot(void) {}
void iap_stop(void) {}

static int output_led(bool on)
{
    int err = status_phase_get() == STATUS_STARTING ? service(LED_START) :
              status_phase_get() == STATUS_CHECKING_HEALTH ? service(LED) : 0;
    if (!err) { err = led_set_error; }
    if (!err) { indicator_edges += indicator_on != on; indicator_on = on; }
    return err;
}
static int output_on(const struct device *dev, uint32_t led)
{
    ARG_UNUSED(dev); zassert_equal(led, 0);
    return output_led(true);
}
static int output_off(const struct device *dev, uint32_t led)
{
    ARG_UNUSED(dev); zassert_equal(led, 0);
    return output_led(false);
}
static DEVICE_API(led, status_led_api) = {.on = output_on, .off = output_off};
DEVICE_DT_DEFINE(DT_NODELABEL(status_leds), NULL, NULL, NULL, NULL,
                  POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &status_led_api);
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

int acquisition_inputs_sample(struct acquisition_sample *snapshot)
{
    if (sample_delay_ms) { k_msleep(sample_delay_ms); }
    snapshot->di = 0x1234;
    snapshot->address = 7;
    for (size_t i = 0; i < SAMPLE_ANALOG_COUNT; ++i) { snapshot->raw[i] = 100 + i; }
    return service(SAMPLE);
}

int points_publish_snapshot(const struct acquisition_sample *snapshot)
{
    published = *snapshot;
    return service(PUBLISH);
}

const struct tr_forward_cfg protocol_forwarding;

int protocol_error(void)
{
    return service(PROTOCOL);
}

int image_confirm_checkpoint(struct image_confirm *state, int64_t now, bool ready)
{
    zassert_not_null(state);
    zassert_true(now >= 0);
    checkpoint_ready = ready;
    return service(CHECKPOINT);
}

static void initialize_fixture(void)
{
    memset(errors, 0, sizeof(errors));
    memset(calls, 0, sizeof(calls));
    published = (struct acquisition_sample){0};
    checks = stops = 0;
    healthy = boot_ready = true;
    dp_health_checks = 0;
    checkpoint_ready = false;
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
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    static bool initialized;
    zassert_false(initialized, "Run cases in separate processes with tests/acquisition/run.py");
    initialized = true;
    initialize_fixture();
}

static struct status_application_fault fault_snapshot(void)
{
    struct status_application_fault fault;
    zassert_ok(status_application_fault_get(&fault));
    return fault;
}

static void check_startup_failure(enum service failed)
{
    errors[failed] = -EIO;
    zassert_equal(startup(), -EIO);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(fault_snapshot().phase, STATUS_STARTING);
    zassert_equal(fault_snapshot().error, -EIO);
    for (enum service s = WATCHDOG_INIT; s <= FORWARD_INIT; ++s) {
        zassert_equal(calls[s], s <= failed ? 1 : 0);
    }
    zassert_equal(stops, 1);
    zassert_equal(checks, 0);
    zassert_equal(acquisition_cycle(), -EACCES);
    zassert_equal(startup(), -EALREADY);
    zassert_equal(fault_snapshot().error, -EIO);
}

ZTEST(acquisition, test_startup_failure_watchdog_init)
{
    check_startup_failure(WATCHDOG_INIT);
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

ZTEST(acquisition, test_success_publishes_complete_sample_and_checks_trial_health)
{
    zassert_ok(startup());
    zassert_equal(status_phase_get(), STATUS_RUN);
    zassert_equal(workers_started, 3);
    zassert_ok(acquisition_cycle());
    zassert_equal(status_phase_get(), STATUS_RUN);
    zassert_true(published.valid);
    zassert_equal(published.di, 0x1234);
    zassert_equal(published.address, 7);
    for (size_t i = 0; i < SAMPLE_ANALOG_COUNT; ++i) {
        zassert_equal(published.raw[i], 100 + i);
    }
    zassert_equal(checks, 1);
    zassert_equal(stops, 0);
    zassert_equal(calls[CHECKPOINT], 1);
    zassert_true(checkpoint_ready);
    boot_ready = false;
    zassert_ok(acquisition_cycle());
    zassert_false(checkpoint_ready);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(acquisition, test_failed_acquisition_publishes_invalid_quality_and_preserves_error)
{
    zassert_ok(startup());
    errors[SAMPLE] = -ENODEV;
    errors[PUBLISH] = -ENOMEM;
    zassert_equal(acquisition_cycle(), -ENODEV);
    zassert_equal(calls[PUBLISH], 1);
    zassert_false(published.valid);
    zassert_equal(fault_snapshot().phase, STATUS_SAMPLING);
    zassert_equal(fault_snapshot().error, -ENODEV);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(checks, 0);
    zassert_equal(stops, 1);
}

static void check_cycle_failure(enum service failed, enum status_phase phase)
{
    zassert_ok(startup());
    errors[failed] = -EIO;
    zassert_equal(acquisition_cycle(), -EIO);
    zassert_equal(checks, 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(stops, 1);
    zassert_equal(fault_snapshot().phase, phase);
    zassert_equal(acquisition_cycle(), -EACCES);
    zassert_equal(calls[PUBLISH], 1);
}

ZTEST(acquisition, test_publish_failure_is_terminal)
{
    check_cycle_failure(PUBLISH, STATUS_PUBLISHING);
}

ZTEST(acquisition, test_protocol_failure_is_terminal)
{
    check_cycle_failure(PROTOCOL, STATUS_SERVICING_COMMUNICATION);
}

ZTEST(acquisition, test_unregistered_dp_has_no_implicit_deadline)
{
    zassert_ok(startup());
    healthy = false;
    zassert_ok(acquisition_cycle());
    zassert_equal(dp_health_checks, 0);
    zassert_equal(stops, 0);
    zassert_equal(status_phase_get(), STATUS_RUN);
}

ZTEST(acquisition, test_confirmation_failure_stops_watchdog)
{
    zassert_ok(startup());
    errors[CHECKPOINT] = -EIO;
    zassert_equal(acquisition_cycle(), -EIO);
    zassert_equal(stops, 1);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(acquisition_cycle(), -EACCES);
}

ZTEST(acquisition, test_gpio_watchdog_error_prevents_trial_confirmation)
{
    zassert_ok(startup());
    errors[WATCHDOG_CHECK] = -EIO;
    zassert_equal(acquisition_cycle(), -EIO);
    zassert_equal(fault_snapshot().phase, STATUS_CHECKING_HEALTH);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_lifecycle_guards)
{
    zassert_equal(status_phase_get(), STATUS_UNCONFIGURED);
    zassert_equal(status_progress(), -EACCES);
    zassert_equal(status_report_fault(STATUS_FAULT_SAMPLE, STATUS_SAMPLING, -EIO), -EACCES);
    zassert_equal(acquisition_cycle(), -EACCES);
    zassert_equal(calls[SAMPLE], 0);
    zassert_ok(startup());
    zassert_equal(startup(), -EALREADY);
    zassert_equal(status_init(), -EALREADY);
    zassert_equal(status_phase_get(), STATUS_RUN);
    struct status_application_fault fault;
    zassert_equal(status_application_fault_get(&fault), -ENOENT);
    zassert_equal(calls[TABLES_INIT], 1);
    zassert_equal(calls[WATCHDOG_INIT], 1);
    zassert_equal(calls[LED_START], 1);
}

ZTEST(acquisition, test_status_retains_original_fault_despite_indicator_failure)
{
    zassert_ok(startup());
    zassert_true(indicator_on);
    led_set_error = -ENODEV; /* Fault indication itself cannot hide sampling error. */
    errors[SAMPLE] = -EIO;
    zassert_equal(acquisition_cycle(), -EIO);
    (void)status_report_fault(STATUS_FAULT_TASK_TIMEOUT, STATUS_CHECKING_HEALTH, -ETIMEDOUT);
    status_set_phase(STATUS_RUN);
    zassert_equal(status_phase_get(), STATUS_FAULT_LOCKED);
    zassert_equal(fault_snapshot().phase, STATUS_SAMPLING);
    zassert_equal(fault_snapshot().error, -EIO);
    zassert_equal(status_progress(), -EACCES);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_led_tracks_startup_healthy_cycles_and_fault)
{
    zassert_ok(startup());
    zassert_true(indicator_on);
    zassert_ok(acquisition_cycle());
    zassert_true(indicator_on); /* Sampling no longer determines blink speed. */
    k_sleep(K_MSEC(STATUS_LED_HALF_PERIOD_MS));
    zassert_false(indicator_on);
    zassert_ok(acquisition_cycle());
    zassert_false(indicator_on);
    k_sleep(K_MSEC(STATUS_LED_HALF_PERIOD_MS));
    zassert_true(indicator_on);
    (void)status_report_fault(STATUS_FAULT_SAMPLE, STATUS_SAMPLING, -EIO);
    zassert_true(indicator_on);
    k_sleep(K_MSEC(STATUS_LED_HALF_PERIOD_MS));
    zassert_true(indicator_on);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_indicator_progresses_during_blocking_sampling)
{
    zassert_ok(startup());
    unsigned before = indicator_edges;
    sample_delay_ms = 350;
    zassert_ok(acquisition_cycle());
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

ZTEST(acquisition, test_late_indicator_skips_edges_and_fault_takes_priority)
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
    zassert_equal(indicator_edges, before + 1); /* Skip, rather than replay, three missed edges. */
    zassert_false(indicator_on);
    (void)status_report_fault(STATUS_FAULT_SAMPLE, STATUS_SAMPLING, -EIO);
    k_msleep(10);
    zassert_true(indicator_on);
    unsigned fault_edges = indicator_edges;
    k_msleep(250);
    zassert_true(indicator_on);
    zassert_equal(indicator_edges, fault_edges);
}

ZTEST(acquisition, test_initial_led_output_failure_stops_startup)
{
    led_set_error = -EIO;
    zassert_equal(startup(), -EIO);
    zassert_equal(calls[INPUTS_INIT], 0);
    zassert_equal(stops, 1);
    zassert_equal(fault_snapshot().phase, STATUS_STARTING);
}

ZTEST(acquisition, test_storage_failure_keeps_sampling_without_confirmation)
{
    errors[PARAMS_INIT] = -EIO;
    parameter_flags = 0;
    zassert_ok(startup());
    zassert_equal(calls[PARAMS_RECOVER], 0);
    zassert_equal(calls[PROCESS_INIT], 1);
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(acquisition_cycle());
    zassert_false(checkpoint_ready);
    zassert_equal(status_degradation_get().storage_error, -EIO);
    zassert_equal(checks, 1);
}

ZTEST(acquisition, test_pending_recovery_prevents_confirmation_until_resolved)
{
    errors[PARAMS_RECOVER] = -ETIMEDOUT;
    parameter_flags = DP_PARAM_STORAGE_READY | DP_PARAM_PENDING;
    zassert_ok(startup());
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(acquisition_cycle());
    zassert_false(checkpoint_ready);

    parameter_flags = DP_PARAM_STORAGE_READY;
    zassert_ok(acquisition_cycle());
    zassert_true(checkpoint_ready);
}

ZTEST(acquisition, test_remote_outage_is_diagnostic_and_keeps_watchdog_progress)
{
    zassert_ok(startup());
    remote_state = CO_DEVICE_OFFLINE;
    zassert_ok(acquisition_cycle());
    zassert_equal(status_degradation_get().remote_offline, 1);
    zassert_equal(checks, 1);
    zassert_equal(stops, 0);
    remote_state = CO_DEVICE_INITIALIZING;
    zassert_ok(acquisition_cycle());
    zassert_equal(status_degradation_get().remote_offline, 0);
    zassert_equal(status_degradation_get().remote_initializing, 1);
    zassert_equal(checks, 2);
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

ZTEST(acquisition, test_storage_fault_survives_successful_operation_until_storage_recovers)
{
    errors[PARAMS_INIT] = -EIO;
    parameter_flags = 0;
    zassert_ok(startup());
    zassert_ok(acquisition_cycle());
    zassert_equal(status_degradation_get().storage_error, -EIO);
    errors[PARAMS_INIT] = 0; /* The most recent parameter operation now succeeded. */
    zassert_ok(acquisition_cycle());
    zassert_equal(status_degradation_get().storage_error, -EIO);
    zassert_false(checkpoint_ready);
    zassert_equal(checks, 2);
    parameter_flags = DP_PARAM_STORAGE_READY;
    zassert_ok(acquisition_cycle());
    zassert_equal(status_degradation_get().storage_error, 0);
    zassert_true(checkpoint_ready);
}

ZTEST_SUITE(acquisition, NULL, NULL, before, NULL, NULL);
