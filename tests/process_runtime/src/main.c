#include "protocol/inc/protocol.h"
#include "protocol/inc/process_table.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

ZTEST(process_runtime, test_local_process_without_communication)
{
    zassert_equal(protocol_local_table.point_count, PROCESS_SIGNAL_COUNT);
    zassert_equal(protocol_local_table.points[PROCESS_DI].signal_key, SIGNAL_DI);
    zassert_equal(
        protocol_local_table.points[PROCESS_SAMPLE_PERIOD].signal_key, SIGNAL_SAMPLE_PERIOD);
    zassert_equal(protocol_local_table.points[PROCESS_CURRENT_8].signal_key, SIGNAL_CURRENT_8);
    zassert_ok(protocol_init());

    const value_t value = {.u16 = 0x55aa};
    value_t result;
    zassert_ok(dp_set_para(DEVICE_LOCAL, SIGNAL_DI, &value));
    zassert_ok(dp_get_para(DEVICE_LOCAL, SIGNAL_DI, &result));
    zassert_equal(result.u16, value.u16);

    value_t period;
    zassert_ok(dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, &period));
    zassert_equal(period.u16, CONFIG_IDU_SAMPLE_PERIOD_MS);

    zassert_equal(protocol_init(), -EEXIST);
}

ZTEST_SUITE(process_runtime, NULL, NULL, NULL, NULL, NULL);
