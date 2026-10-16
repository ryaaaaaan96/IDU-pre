#include "idu_boot.h"
#include <zephyr/ztest.h>

static unsigned writes;
static int write_error;
static bool already_confirmed;
static struct idu_boot_health state;
bool boot_is_img_confirmed(void) { return already_confirmed; }
int boot_write_img_confirmed(void) { ++writes; return write_error; }
#define checkpoint(now, healthy) idu_boot_health_checkpoint(&state, now, healthy)

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    state = (struct idu_boot_health){0};
    writes = 0; write_error = 0; already_confirmed = false;
}

ZTEST(boot_policy, test_trial_only_confirmed_after_continuous_health)
{
    /* Simply waiting at boot cannot confirm an image. */
    zassert_ok(checkpoint(30000, true));
    zassert_equal(writes, 0);
    for (int64_t now = 30500; now < 40000; now += 500) {
        zassert_ok(checkpoint(now, true));
    }
    /* One failed cycle discards accumulated health. */
    zassert_ok(checkpoint(40000, false));
    zassert_ok(checkpoint(40500, true));
    /* A stalled loop also restarts the qualification period. */
    zassert_ok(checkpoint(45000, true));
    for (int64_t now = 45500; now < 55000; now += 500) {
        zassert_ok(checkpoint(now, true));
    }
    zassert_equal(writes, 0);
    write_error = -EIO;
    zassert_equal(checkpoint(55000, true), -EIO);
    zassert_equal(writes, 1);
    /* A storage error is returned to the main loop, never reported as success. */
    write_error = 0;
    zassert_ok(checkpoint(55500, true));
    zassert_equal(writes, 2);
    zassert_ok(checkpoint(56000, true));
    zassert_equal(writes, 2);
}

ZTEST(boot_policy, test_unhealthy_trial_times_out)
{
    zassert_ok(checkpoint(1000, false));
    zassert_ok(checkpoint(60999, false));
    zassert_equal(checkpoint(61000, false), -ETIMEDOUT);
    zassert_equal(writes, 0);
}

ZTEST(boot_policy, test_confirmed_image_needs_no_trial_deadline)
{
    already_confirmed = true;
    zassert_ok(checkpoint(1000, true));
    zassert_ok(checkpoint(100000, false));
    zassert_equal(writes, 0);
}

ZTEST_SUITE(boot_policy, NULL, NULL, before, NULL, NULL);
