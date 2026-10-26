#include "status/image_confirm.h"
#include <zephyr/ztest.h>

static unsigned writes;
static int write_error;
static bool already_confirmed;
static struct image_confirm state;
bool boot_is_img_confirmed(void) { return already_confirmed; }
int boot_write_img_confirmed(void) { ++writes; return write_error; }
#define checkpoint(now, healthy) image_confirm_checkpoint(&state, now, healthy)

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    state = (struct image_confirm){0};
    writes = 0; write_error = 0; already_confirmed = false;
}

ZTEST(image_confirm, test_trial_only_confirmed_after_continuous_health)
{
    /* 仅在启动后等待不能确认镜像。 */
    zassert_ok(checkpoint(30000, true));
    zassert_equal(writes, 0);
    for (int64_t now = 30500; now < 40000; now += 500) {
        zassert_ok(checkpoint(now, true));
    }
    /* 一个失败周期会清除已累计的健康时长。 */
    zassert_ok(checkpoint(40000, false));
    zassert_ok(checkpoint(40500, true));
    /* 循环停滞也会重新开始资格判定周期。 */
    zassert_ok(checkpoint(45000, true));
    for (int64_t now = 45500; now < 55000; now += 500) {
        zassert_ok(checkpoint(now, true));
    }
    zassert_equal(writes, 0);
    write_error = -EIO;
    zassert_equal(checkpoint(55000, true), -EIO);
    zassert_equal(writes, 1);
    /* 存储错误返回主循环，不得报告成功。 */
    write_error = 0;
    zassert_ok(checkpoint(55500, true));
    zassert_equal(writes, 2);
    zassert_ok(checkpoint(56000, true));
    zassert_equal(writes, 2);
}

ZTEST(image_confirm, test_unhealthy_trial_times_out)
{
    zassert_ok(checkpoint(1000, false));
    zassert_ok(checkpoint(60999, false));
    zassert_equal(checkpoint(61000, false), -ETIMEDOUT);
    zassert_equal(writes, 0);
}

ZTEST(image_confirm, test_confirmed_image_needs_no_trial_deadline)
{
    already_confirmed = true;
    zassert_ok(checkpoint(1000, true));
    zassert_ok(checkpoint(100000, false));
    zassert_equal(writes, 0);
}

ZTEST_SUITE(image_confirm, NULL, NULL, before, NULL, NULL);
