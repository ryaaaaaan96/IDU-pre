#include "protocol/inc/protocol.h"
#include <transmit/forward.h>
#include "acquisition/idu_acquisition.h"
#include "process/idu_process.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/params.h>
#include <dataprocess/process.h>
#include <platform_callback.h>
#include <gpio/idu_inputs.h>
#include <iap/iap.h>
#include <zephyr/drivers/led.h>
#include <watchdog/watchdog.h>
#include <zephyr/ztest.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

/* main.c keeps startup local to the application entry, without a public header. */
int idu_startup(struct idu_status *status);

enum service { WATCHDOG_INIT, LED_START, INPUTS_INIT, TABLES_INIT, DP_INIT,
               NETWORK_INIT, PORT_OPEN, COLLECT_INIT, PARAMS_INIT, PARAMS_RECOVER,
               PROCESS_INIT, REFERENCE_INIT, COLLECT_START, IAP_INIT, FORWARD_INIT,
               SAMPLE, PUBLISH, PROTOCOL, LED, CHECKPOINT, WATCHDOG_PROGRESS, SERVICES };
static int errors[SERVICES];
static unsigned calls[SERVICES], kicks, stops;
static bool healthy, boot_ready, checkpoint_ready;
static int led_set_error;
static bool indicator_on;
static struct idu_snapshot published;
static struct idu_status acquisition;
static int previous_startup_service;
static uint16_t parameter_flags;
static const struct dp_equip_cfg point_config;
const struct co_bus_cfg idu_collect_bus = {0};
const struct co_port_config idu_collect_port;

static int service(enum service service)
{
    static const enum idu_status_phase phases[SERVICES] = {
        [WATCHDOG_INIT] = IDU_STATUS_STARTING,
        [LED_START] = IDU_STATUS_STARTING,
        [INPUTS_INIT] = IDU_STATUS_STARTING,
        [TABLES_INIT] = IDU_STATUS_STARTING,
        [DP_INIT] = IDU_STATUS_STARTING,
        [NETWORK_INIT] = IDU_STATUS_STARTING,
        [PORT_OPEN] = IDU_STATUS_STARTING,
        [COLLECT_INIT] = IDU_STATUS_STARTING,
        [PARAMS_INIT] = IDU_STATUS_STARTING,
        [PARAMS_RECOVER] = IDU_STATUS_STARTING,
        [PROCESS_INIT] = IDU_STATUS_STARTING,
        [REFERENCE_INIT] = IDU_STATUS_STARTING,
        [COLLECT_START] = IDU_STATUS_STARTING,
        [IAP_INIT] = IDU_STATUS_STARTING,
        [FORWARD_INIT] = IDU_STATUS_STARTING,
        [SAMPLE] = IDU_STATUS_SAMPLING,
        [PUBLISH] = IDU_STATUS_PUBLISHING,
        [PROTOCOL] = IDU_STATUS_SERVICING_COMMUNICATION,
        [LED] = IDU_STATUS_CHECKING_HEALTH,
        [CHECKPOINT] = IDU_STATUS_CHECKING_HEALTH,
        [WATCHDOG_PROGRESS] = IDU_STATUS_CHECKING_HEALTH,
    };
    zassert_equal(acquisition.phase, phases[service]);
    if (service <= FORWARD_INIT) {
        zassert_true((int)service > previous_startup_service, "Startup order changed");
        previous_startup_service = service;
    }
    ++calls[service];
    return errors[service];
}

int watchdog_start(struct watchdog *monitor, const struct watchdog_config *config)
{ return service(WATCHDOG_INIT); }
int idu_inputs_init(void) { return service(INPUTS_INIT); }
int idu_register_point_tables(void) { return service(TABLES_INIT); }
int idu_connect_point_services(void) { return 0; }
void idu_start_point_tasks(void) {}
void idu_start_collect_task(void) {}
int dp_init(void) { return service(DP_INIT); }
const struct dp_equip_cfg *dp_config(void) { return &point_config; }
int idu_ports_init(void)
{
    return service(NETWORK_INIT);
}
int co_port_open(const struct co_port_config *config)
{
    zassert_equal_ptr(config, &idu_collect_port);
    int err = service(PORT_OPEN);
    return err ? err : 3;
}
int co_init_bus(const struct dp_equip_cfg *config, const struct co_bus_cfg *bus)
{
    zassert_equal_ptr(config, &point_config);
    zassert_equal_ptr(bus, &idu_collect_bus);
    return service(COLLECT_INIT);
}
int dp_params_init(const struct dp_equip_cfg *config, uint8_t area_id)
{
    zassert_equal_ptr(config, &point_config);
    zassert_equal(area_id, DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)));
    return service(PARAMS_INIT);
}
int dp_params_recover(void) { return service(PARAMS_RECOVER); }
int dp_process_start(const struct dp_process_plan *plan)
{
    zassert_true(plan->initialize_all);
    return service(PROCESS_INIT);
}
int dp_reference_start(void) { return service(REFERENCE_INIT); }
int co_start(void) { return service(COLLECT_START); }
uint16_t idu_sample_period_ms(void) { return 500; }
int iap_start(void) { return service(IAP_INIT); }
int idu_start_forwarding(void) { return service(FORWARD_INIT); }
int watchdog_progress(struct watchdog *monitor) { ++kicks; return service(WATCHDOG_PROGRESS); }
void watchdog_stop(struct watchdog *monitor) { ++stops; }

static int output_led(bool on)
{
    int err = acquisition.phase == IDU_STATUS_STARTING ? service(LED_START) :
              acquisition.phase == IDU_STATUS_CHECKING_HEALTH ? service(LED) : 0;
    if (!err) { err = led_set_error; }
    if (!err) { indicator_on = on; }
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
    };
}
bool dp_healthy(uint32_t max_idle_ms)
{
    zassert_equal(max_idle_ms, 1500);
    return healthy;
}

int idu_inputs_sample(struct idu_snapshot *snapshot)
{
    snapshot->di = 0x1234;
    snapshot->address = 7;
    for (size_t i = 0; i < IDU_ANALOG_COUNT; ++i) { snapshot->raw[i] = 100 + i; }
    return service(SAMPLE);
}

int idu_snapshot_publish(const struct idu_snapshot *snapshot)
{
    published = *snapshot;
    return service(PUBLISH);
}

const struct tr_forward_cfg idu_forwarding;

int idu_protocol_error(void)
{
    return service(PROTOCOL);
}

int idu_image_confirm_checkpoint(struct idu_image_confirm *state, int64_t now, bool ready)
{
    zassert_equal_ptr(state, &acquisition.image_confirm);
    zassert_true(now >= 0);
    checkpoint_ready = ready;
    return service(CHECKPOINT);
}

static void reset(void)
{
    memset(errors, 0, sizeof(errors));
    memset(calls, 0, sizeof(calls));
    acquisition = (struct idu_status){0};
    published = (struct idu_snapshot){0};
    kicks = stops = 0;
    healthy = boot_ready = true;
    checkpoint_ready = false;
    led_set_error = 0;
    indicator_on = false;
    previous_startup_service = -1;
    parameter_flags = DP_PARAM_STORAGE_READY;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    reset();
}

ZTEST(acquisition, test_startup_failure_stops_before_later_services)
{
    for (enum service failed = WATCHDOG_INIT; failed <= FORWARD_INIT; ++failed) {
        /* Storage failures retain the product's read-only degraded mode. */
        if (failed == PARAMS_INIT || failed == PARAMS_RECOVER) {
            continue;
        }
        reset();
        errors[failed] = -EIO;
        zassert_equal(idu_startup(&acquisition), -EIO);
        zassert_equal(acquisition.phase, IDU_STATUS_FAULT_LOCKED);
        zassert_equal(acquisition.fault_phase, IDU_STATUS_STARTING);
        zassert_equal(acquisition.last_error, -EIO);
        for (enum service s = WATCHDOG_INIT; s <= FORWARD_INIT; ++s) {
            zassert_equal(calls[s], s <= failed ? 1 : 0);
        }
        zassert_equal(stops, 1);
        zassert_equal(kicks, 0);
        zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
        zassert_equal(idu_startup(&acquisition), -EALREADY);
    }
}

ZTEST(acquisition, test_success_publishes_complete_sample_and_checks_trial_health)
{
    zassert_ok(idu_startup(&acquisition));
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_true(published.valid);
    zassert_equal(published.di, 0x1234);
    zassert_equal(published.address, 7);
    for (size_t i = 0; i < IDU_ANALOG_COUNT; ++i) {
        zassert_equal(published.raw[i], 100 + i);
    }
    zassert_equal(kicks, 1);
    zassert_equal(stops, 0);
    zassert_equal(calls[CHECKPOINT], 1);
    zassert_true(checkpoint_ready);
    boot_ready = false;
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_false(checkpoint_ready);
    zassert_equal(acquisition.phase, IDU_STATUS_WAITING);
}

ZTEST(acquisition, test_failed_acquisition_publishes_invalid_quality_and_preserves_error)
{
    zassert_ok(idu_startup(&acquisition));
    errors[SAMPLE] = -ENODEV;
    errors[PUBLISH] = -ENOMEM;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ENODEV);
    zassert_equal(calls[PUBLISH], 1);
    zassert_false(published.valid);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_SAMPLING);
    zassert_equal(acquisition.last_error, -ENODEV);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(kicks, 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_cycle_errors_cannot_feed_or_confirm_and_stop_is_terminal)
{
    const enum service failures[] = {PUBLISH, PROTOCOL, LED};
    for (size_t i = 0; i < ARRAY_SIZE(failures); ++i) {
        reset();
        zassert_ok(idu_startup(&acquisition));
        errors[failures[i]] = -EIO;
        zassert_equal(idu_acquisition_cycle(&acquisition), -EIO);
        zassert_equal(kicks, 0);
        zassert_equal(calls[CHECKPOINT], 0);
        zassert_equal(stops, 1);
        zassert_equal(acquisition.fault_phase, failures[i] == PUBLISH ?
                      IDU_STATUS_PUBLISHING : failures[i] == LED ?
                      IDU_STATUS_CHECKING_HEALTH : IDU_STATUS_SERVICING_COMMUNICATION);
        zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
        zassert_equal(calls[PUBLISH], 1);
    }
}

ZTEST(acquisition, test_stalled_processing_stops_before_protocol_and_confirmation)
{
    zassert_ok(idu_startup(&acquisition));
    healthy = false;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ETIMEDOUT);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(kicks, 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_confirmation_failure_stops_watchdog)
{
    zassert_ok(idu_startup(&acquisition));
    errors[CHECKPOINT] = -EIO;
    zassert_equal(idu_acquisition_cycle(&acquisition), -EIO);
    zassert_equal(stops, 1);
    zassert_equal(acquisition.phase, IDU_STATUS_FAULT_LOCKED);
    zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
}

ZTEST(acquisition, test_expired_watchdog_prevents_trial_confirmation)
{
    zassert_ok(idu_startup(&acquisition));
    errors[WATCHDOG_PROGRESS] = -ETIMEDOUT;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ETIMEDOUT);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_CHECKING_HEALTH);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_lifecycle_guards)
{
    zassert_equal(idu_startup(NULL), -EINVAL);
    zassert_equal(idu_acquisition_cycle(NULL), -EINVAL);
    zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
    zassert_equal(calls[SAMPLE], 0);
    zassert_ok(idu_startup(&acquisition));
    zassert_equal(idu_startup(&acquisition), -EALREADY);
    zassert_equal(calls[TABLES_INIT], 1);
}

ZTEST(acquisition, test_status_retains_original_fault_despite_indicator_failure)
{
    zassert_ok(idu_startup(&acquisition));
    zassert_true(indicator_on);
    led_set_error = -ENODEV; /* Fault indication itself cannot hide sampling error. */
    errors[SAMPLE] = -EIO;
    zassert_equal(idu_acquisition_cycle(&acquisition), -EIO);
    idu_status_fail(&acquisition, IDU_STATUS_CHECKING_HEALTH, -ETIMEDOUT);
    idu_status_set_phase(&acquisition, IDU_STATUS_WAITING);
    zassert_equal(acquisition.phase, IDU_STATUS_FAULT_LOCKED);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_SAMPLING);
    zassert_equal(acquisition.last_error, -EIO);
    zassert_equal(idu_status_progress(&acquisition), -EACCES);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_led_tracks_startup_healthy_cycles_and_fault)
{
    zassert_ok(idu_startup(&acquisition));
    zassert_true(indicator_on);
    zassert_true(acquisition.led_on);
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_false(indicator_on);
    zassert_false(acquisition.led_on);
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_true(indicator_on);
    idu_status_fail(&acquisition, IDU_STATUS_SAMPLING, -EIO);
    zassert_true(indicator_on);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_initial_led_output_failure_stops_startup)
{
    led_set_error = -EIO;
    zassert_equal(idu_startup(&acquisition), -EIO);
    zassert_equal(calls[INPUTS_INIT], 0);
    zassert_equal(stops, 1);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_STARTING);
}

ZTEST(acquisition, test_storage_failure_keeps_sampling_without_confirmation)
{
    errors[PARAMS_INIT] = -EIO;
    parameter_flags = 0;
    zassert_ok(idu_startup(&acquisition));
    zassert_equal(calls[PARAMS_RECOVER], 0);
    zassert_equal(calls[PROCESS_INIT], 1);
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_false(checkpoint_ready);
    zassert_equal(kicks, 1);
}

ZTEST(acquisition, test_pending_recovery_prevents_confirmation_until_resolved)
{
    errors[PARAMS_RECOVER] = -ETIMEDOUT;
    parameter_flags = DP_PARAM_STORAGE_READY | DP_PARAM_PENDING;
    zassert_ok(idu_startup(&acquisition));
    zassert_equal(calls[COLLECT_START], 1);
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_false(checkpoint_ready);

    parameter_flags = DP_PARAM_STORAGE_READY;
    zassert_ok(idu_acquisition_cycle(&acquisition));
    zassert_true(checkpoint_ready);
}

ZTEST_SUITE(acquisition, NULL, NULL, before, NULL, NULL);
