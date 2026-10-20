#include "cfg/inc/ports.h"
#include <transmit/server.h>
#include "acquisition/idu_acquisition.h"
#include "process/idu_process.h"
#include <gpio/idu_inputs.h>
#include <iap/iap.h>
#include <zephyr/drivers/led.h>
#include <watchdog/watchdog.h>
#include <zephyr/ztest.h>
#include <string.h>

enum service { WATCHDOG_INIT, LED_START, INPUTS_INIT, EQUIPMENT_INIT, IAP_INIT,
               SAMPLE, PUBLISH, PROTOCOL, LED, CHECKPOINT, WATCHDOG_PROGRESS, SERVICES };
static int errors[SERVICES];
static unsigned calls[SERVICES], kicks, stops;
static bool healthy, boot_ready, checkpoint_ready;
static int led_set_error;
static bool indicator_on;
static struct idu_snapshot published;
static struct idu_status acquisition;

static int service(enum service service)
{
    static const enum idu_status_phase phases[SERVICES] = {
        [WATCHDOG_INIT] = IDU_STATUS_STARTING,
        [LED_START] = IDU_STATUS_STARTING,
        [INPUTS_INIT] = IDU_STATUS_STARTING,
        [EQUIPMENT_INIT] = IDU_STATUS_STARTING,
        [IAP_INIT] = IDU_STATUS_STARTING,
        [SAMPLE] = IDU_STATUS_SAMPLING,
        [PUBLISH] = IDU_STATUS_PUBLISHING,
        [PROTOCOL] = IDU_STATUS_SERVICING_COMMUNICATION,
        [LED] = IDU_STATUS_CHECKING_HEALTH,
        [CHECKPOINT] = IDU_STATUS_CHECKING_HEALTH,
        [WATCHDOG_PROGRESS] = IDU_STATUS_CHECKING_HEALTH,
    };
    zassert_equal(acquisition.phase, phases[service]);
    ++calls[service];
    return errors[service];
}

int watchdog_start(struct watchdog *monitor, const struct watchdog_config *config)
{ return service(WATCHDOG_INIT); }
int idu_inputs_init(void) { return service(INPUTS_INIT); }
int idu_equipment_init(void) { return service(EQUIPMENT_INIT); }
int iap_start(void) { return service(IAP_INIT); }
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
bool idu_equipment_confirm_ready(void) { return boot_ready; }
bool idu_equipment_healthy(uint32_t max_idle_ms)
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

const struct tr_server_config idu_forwarding;

int tr_server_service(const struct tr_server_config *config, uint8_t address)
{
    zassert_equal_ptr(config, &idu_forwarding);
    zassert_equal(address, 7);
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
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    reset();
}

ZTEST(acquisition, test_startup_failure_stops_before_later_services)
{
    for (enum service failed = WATCHDOG_INIT; failed <= IAP_INIT; ++failed) {
        reset();
        errors[failed] = -EIO;
        zassert_equal(idu_acquisition_start(&acquisition), -EIO);
        zassert_equal(acquisition.phase, IDU_STATUS_FAULT_LOCKED);
        zassert_equal(acquisition.fault_phase, IDU_STATUS_STARTING);
        zassert_equal(acquisition.last_error, -EIO);
        for (enum service s = WATCHDOG_INIT; s <= IAP_INIT; ++s) {
            zassert_equal(calls[s], s <= failed ? 1 : 0);
        }
        zassert_equal(stops, 1);
        zassert_equal(kicks, 0);
        zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
        zassert_equal(idu_acquisition_start(&acquisition), -EALREADY);
    }
}

ZTEST(acquisition, test_success_publishes_complete_sample_and_checks_trial_health)
{
    zassert_ok(idu_acquisition_start(&acquisition));
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
    zassert_ok(idu_acquisition_start(&acquisition));
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
        zassert_ok(idu_acquisition_start(&acquisition));
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
    zassert_ok(idu_acquisition_start(&acquisition));
    healthy = false;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ETIMEDOUT);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(kicks, 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_confirmation_failure_stops_watchdog)
{
    zassert_ok(idu_acquisition_start(&acquisition));
    errors[CHECKPOINT] = -EIO;
    zassert_equal(idu_acquisition_cycle(&acquisition), -EIO);
    zassert_equal(stops, 1);
    zassert_equal(acquisition.phase, IDU_STATUS_FAULT_LOCKED);
    zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
}

ZTEST(acquisition, test_expired_watchdog_prevents_trial_confirmation)
{
    zassert_ok(idu_acquisition_start(&acquisition));
    errors[WATCHDOG_PROGRESS] = -ETIMEDOUT;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ETIMEDOUT);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_CHECKING_HEALTH);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(stops, 1);
}

ZTEST(acquisition, test_lifecycle_guards)
{
    zassert_equal(idu_acquisition_start(NULL), -EINVAL);
    zassert_equal(idu_acquisition_cycle(NULL), -EINVAL);
    zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
    zassert_equal(calls[SAMPLE], 0);
    zassert_ok(idu_acquisition_start(&acquisition));
    zassert_equal(idu_acquisition_start(&acquisition), -EALREADY);
    zassert_equal(calls[EQUIPMENT_INIT], 1);
}

ZTEST(acquisition, test_status_retains_original_fault_despite_indicator_failure)
{
    zassert_ok(idu_acquisition_start(&acquisition));
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
    zassert_ok(idu_acquisition_start(&acquisition));
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
    zassert_equal(idu_acquisition_start(&acquisition), -EIO);
    zassert_equal(calls[INPUTS_INIT], 0);
    zassert_equal(stops, 1);
    zassert_equal(acquisition.fault_phase, IDU_STATUS_STARTING);
}

ZTEST_SUITE(acquisition, NULL, NULL, before, NULL, NULL);
