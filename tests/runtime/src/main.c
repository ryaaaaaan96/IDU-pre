#include "idu_runtime.h"
#include "idu_equipment.h"
#include "hardware/idu_pre.h"
#include "iap/idu_iap.h"
#include <zephyr/ztest.h>
#include <string.h>

enum service { HEALTH_INIT, IO_INIT, ANALOG_INIT, EQUIPMENT_INIT, IAP_INIT,
               IO_READ, ANALOG_READ, PUBLISH, PROTOCOL, LED, CHECKPOINT, SERVICES };
static int errors[SERVICES];
static unsigned calls[SERVICES], kicks, stops;
static bool healthy, boot_ready, checkpoint_ready;
static struct idu_snapshot published;
static struct idu_runtime runtime;

static int service(enum service service)
{
    ++calls[service];
    return errors[service];
}

int idu_health_init(void) { return service(HEALTH_INIT); }
int idu_io_init(void) { return service(IO_INIT); }
int idu_analog_init(void) { return service(ANALOG_INIT); }
int idu_equipment_init(void) { return service(EQUIPMENT_INIT); }
int idu_iap_init(void) { return service(IAP_INIT); }
void idu_health_kick(void) { ++kicks; }
void idu_health_stop(void) { ++stops; }
int idu_led_toggle(void) { return service(LED); }
bool idu_equipment_boot_ready(void) { return boot_ready; }
bool idu_equipment_healthy(uint32_t max_idle_ms)
{
    zassert_equal(max_idle_ms, 1500);
    return healthy;
}

int idu_io_read(uint16_t *di, uint8_t *address)
{
    *di = 0x1234;
    *address = 7;
    return service(IO_READ);
}

int idu_analog_read(uint16_t raw[IDU_ANALOG_COUNT])
{
    for (size_t i = 0; i < IDU_ANALOG_COUNT; ++i) { raw[i] = 100 + i; }
    return service(ANALOG_READ);
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
    zassert_equal_ptr(state, &runtime.boot);
    zassert_true(now >= 0);
    checkpoint_ready = ready;
    return service(CHECKPOINT);
}

static void reset(void)
{
    memset(errors, 0, sizeof(errors));
    memset(calls, 0, sizeof(calls));
    runtime = (struct idu_runtime){0};
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

ZTEST(runtime, test_startup_failure_stops_before_later_services)
{
    for (enum service failed = HEALTH_INIT; failed <= IAP_INIT; ++failed) {
        reset();
        errors[failed] = -EIO;
        zassert_equal(idu_runtime_init(&runtime), -EIO);
        zassert_equal(runtime.state, IDU_RUNTIME_STOPPED);
        for (enum service s = HEALTH_INIT; s <= IAP_INIT; ++s) {
            zassert_equal(calls[s], s <= failed ? 1 : 0);
        }
        zassert_equal(stops, 1);
        zassert_equal(kicks, 0);
        zassert_equal(idu_runtime_step(&runtime), -EACCES);
        zassert_equal(idu_runtime_init(&runtime), -EALREADY);
    }
}

ZTEST(runtime, test_success_publishes_complete_sample_and_checks_trial_health)
{
    zassert_ok(idu_runtime_init(&runtime));
    zassert_ok(idu_runtime_step(&runtime));
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
    zassert_ok(idu_runtime_step(&runtime));
    zassert_false(checkpoint_ready);
    zassert_equal(runtime.state, IDU_RUNTIME_RUNNING);
}

ZTEST(runtime, test_failed_acquisition_publishes_invalid_quality_and_preserves_error)
{
    for (enum service failed = IO_READ; failed <= ANALOG_READ; ++failed) {
        reset();
        zassert_ok(idu_runtime_init(&runtime));
        errors[failed] = -ENODEV;
        errors[PUBLISH] = -ENOMEM;
        zassert_equal(idu_runtime_step(&runtime), -ENODEV);
        zassert_equal(calls[PUBLISH], 1);
        zassert_false(published.valid);
        zassert_equal(calls[ANALOG_READ], failed == ANALOG_READ ? 1 : 0);
        zassert_equal(calls[PROTOCOL], 0);
        zassert_equal(calls[CHECKPOINT], 0);
        zassert_equal(kicks, 0);
        zassert_equal(stops, 1);
    }
}

ZTEST(runtime, test_cycle_errors_cannot_feed_or_confirm_and_stop_is_terminal)
{
    const enum service failures[] = {PUBLISH, PROTOCOL, LED};
    for (size_t i = 0; i < ARRAY_SIZE(failures); ++i) {
        reset();
        zassert_ok(idu_runtime_init(&runtime));
        errors[failures[i]] = -EIO;
        zassert_equal(idu_runtime_step(&runtime), -EIO);
        zassert_equal(kicks, 0);
        zassert_equal(calls[CHECKPOINT], 0);
        zassert_equal(stops, 1);
        zassert_equal(idu_runtime_step(&runtime), -EACCES);
        zassert_equal(calls[PUBLISH], 1);
    }
}

ZTEST(runtime, test_stalled_processing_stops_before_protocol_and_confirmation)
{
    zassert_ok(idu_runtime_init(&runtime));
    healthy = false;
    zassert_equal(idu_runtime_step(&runtime), -ETIMEDOUT);
    zassert_equal(calls[PROTOCOL], 0);
    zassert_equal(calls[CHECKPOINT], 0);
    zassert_equal(kicks, 0);
    zassert_equal(stops, 1);
}

ZTEST(runtime, test_confirmation_failure_stops_watchdog)
{
    zassert_ok(idu_runtime_init(&runtime));
    errors[CHECKPOINT] = -EIO;
    zassert_equal(idu_runtime_step(&runtime), -EIO);
    zassert_equal(stops, 1);
    zassert_equal(runtime.state, IDU_RUNTIME_STOPPED);
    zassert_equal(idu_runtime_step(&runtime), -EACCES);
}

ZTEST(runtime, test_lifecycle_guards)
{
    zassert_equal(idu_runtime_init(NULL), -EINVAL);
    zassert_equal(idu_runtime_step(NULL), -EINVAL);
    zassert_equal(idu_runtime_step(&runtime), -EACCES);
    zassert_equal(calls[IO_READ], 0);
    zassert_ok(idu_runtime_init(&runtime));
    zassert_equal(idu_runtime_init(&runtime), -EALREADY);
    zassert_equal(calls[EQUIPMENT_INIT], 1);
}

ZTEST_SUITE(runtime, NULL, NULL, before, NULL, NULL);
