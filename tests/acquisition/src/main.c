#include "acquisition/idu_acquisition.h"
#include "process/idu_process.h"
#include "communication/idu_protocol.h"
#include "acquisition/inputs.h"
#include <iap/iap.h>
#include <zephyr/ztest.h>
#include <string.h>

enum service { HEALTH_INIT, INPUTS_INIT, EQUIPMENT_INIT, IAP_INIT,
               SAMPLE, PUBLISH, PROTOCOL, LED, CHECKPOINT, WATCHDOG_PROGRESS, SERVICES };
static int errors[SERVICES];
static unsigned calls[SERVICES], kicks, stops;
static bool healthy, boot_ready, checkpoint_ready;
static struct idu_snapshot published;
static struct idu_acquisition acquisition;

static int service(enum service service)
{
    static const enum idu_acquisition_state phases[SERVICES] = {
        [HEALTH_INIT] = IDU_ACQUISITION_STARTING,
        [INPUTS_INIT] = IDU_ACQUISITION_STARTING,
        [EQUIPMENT_INIT] = IDU_ACQUISITION_STARTING,
        [IAP_INIT] = IDU_ACQUISITION_STARTING,
        [SAMPLE] = IDU_ACQUISITION_SAMPLING,
        [PUBLISH] = IDU_ACQUISITION_PUBLISHING,
        [PROTOCOL] = IDU_ACQUISITION_SERVICING_COMMUNICATION,
        [LED] = IDU_ACQUISITION_SERVICING_COMMUNICATION,
        [CHECKPOINT] = IDU_ACQUISITION_CHECKING_HEALTH,
        [WATCHDOG_PROGRESS] = IDU_ACQUISITION_CHECKING_HEALTH,
    };
    zassert_equal(acquisition.state, phases[service]);
    ++calls[service];
    return errors[service];
}

int idu_watchdog_start(void) { return service(HEALTH_INIT); }
int idu_inputs_init(void) { return service(INPUTS_INIT); }
int idu_equipment_init(void) { return service(EQUIPMENT_INIT); }
int iap_start(void) { return service(IAP_INIT); }
int idu_watchdog_progress(void) { ++kicks; return service(WATCHDOG_PROGRESS); }
void idu_watchdog_stop(void) { ++stops; }
uint32_t idu_watchdog_timeout_ms(void) { return 1500; }
int idu_indicator_toggle(void) { return service(LED); }
bool idu_equipment_boot_ready(void) { return boot_ready; }
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

int idu_protocol_set_address(uint8_t address)
{
    zassert_equal(address, 7);
    return service(PROTOCOL);
}

int idu_boot_health_checkpoint(struct idu_boot_health *state, int64_t now, bool ready)
{
    zassert_equal_ptr(state, &acquisition.boot);
    zassert_true(now >= 0);
    checkpoint_ready = ready;
    return service(CHECKPOINT);
}

static void reset(void)
{
    memset(errors, 0, sizeof(errors));
    memset(calls, 0, sizeof(calls));
    acquisition = (struct idu_acquisition){0};
    published = (struct idu_snapshot){0};
    kicks = stops = 0;
    healthy = boot_ready = true;
    checkpoint_ready = false;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    reset();
}

ZTEST(acquisition, test_startup_failure_stops_before_later_services)
{
    for (enum service failed = HEALTH_INIT; failed <= IAP_INIT; ++failed) {
        reset();
        errors[failed] = -EIO;
        zassert_equal(idu_acquisition_start(&acquisition), -EIO);
        zassert_equal(acquisition.state, IDU_ACQUISITION_FAULT_LOCKED);
        zassert_equal(acquisition.fault_phase, IDU_ACQUISITION_STARTING);
        zassert_equal(acquisition.last_error, -EIO);
        for (enum service s = HEALTH_INIT; s <= IAP_INIT; ++s) {
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
    zassert_equal(acquisition.state, IDU_ACQUISITION_WAITING);
}

ZTEST(acquisition, test_failed_acquisition_publishes_invalid_quality_and_preserves_error)
{
    zassert_ok(idu_acquisition_start(&acquisition));
    errors[SAMPLE] = -ENODEV;
    errors[PUBLISH] = -ENOMEM;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ENODEV);
    zassert_equal(calls[PUBLISH], 1);
    zassert_false(published.valid);
    zassert_equal(acquisition.fault_phase, IDU_ACQUISITION_SAMPLING);
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
                      IDU_ACQUISITION_PUBLISHING : IDU_ACQUISITION_SERVICING_COMMUNICATION);
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
    zassert_equal(acquisition.state, IDU_ACQUISITION_FAULT_LOCKED);
    zassert_equal(idu_acquisition_cycle(&acquisition), -EACCES);
}

ZTEST(acquisition, test_expired_watchdog_prevents_trial_confirmation)
{
    zassert_ok(idu_acquisition_start(&acquisition));
    errors[WATCHDOG_PROGRESS] = -ETIMEDOUT;
    zassert_equal(idu_acquisition_cycle(&acquisition), -ETIMEDOUT);
    zassert_equal(acquisition.fault_phase, IDU_ACQUISITION_CHECKING_HEALTH);
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

ZTEST_SUITE(acquisition, NULL, NULL, before, NULL, NULL);
