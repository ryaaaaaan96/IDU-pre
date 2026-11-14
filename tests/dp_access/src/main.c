#include <dataprocess/dataprocess.h>
#include <zephyr/ztest.h>
#include <errno.h>

static unsigned events;
static int point_callback(const dp_callback_context_t *context)
{
    events |= context->events;
    return 0;
}

ZTEST(dp_access, test_value_access)
{
    static const dp_point_config_t points[] = {
        {.signal_key = 10, .size = 2, .type = VALUE_TYPE_U16,
         .flags = DP_POINT_FLAG_CHECK_RANGE,
         .data = {.initial.u16 = 5, .minimum.u16 = 1, .maximum.u16 = 100},
         .access_callback = point_callback},
    };
    const dp_table_t table = {.points = points, .point_count = 1};
    zassert_ok(dp_init());
    zassert_ok(dp_register_device(7, &table));
    dp_key_access_t access = {.device_id = 7, .signal_key = 10,
        .value_type = VALUE_TYPE_U16,
        .flags = DP_ACCESS_FLAG_GET_CALLBACK | DP_ACCESS_FLAG_SET_CALLBACK};
    value_t value = {.u16 = 42};
    zassert_ok(dp_set_value_by_key(&access, &value));
    zassert_true(events & DP_CALLBACK_EVENT_SET);
    value.u16 = 0;
    zassert_ok(dp_get_value_by_key(&access, &value));
    zassert_equal(value.u16, 42);
    zassert_true(events & DP_CALLBACK_EVENT_GET);
    value.u16 = 101;
    zassert_equal(dp_set_value_by_key(&access, &value), -ERANGE);
    zassert_ok(dp_get_value_by_key(&access, &value));
    zassert_equal(value.u16, 42);
    zassert_ok(dp_remove_device(7));
}
ZTEST_SUITE(dp_access, NULL, NULL, NULL, NULL, NULL);
