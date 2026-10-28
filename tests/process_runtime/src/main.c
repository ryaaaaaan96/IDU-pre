#include "protocol/inc/protocol.h"
#include "protocol/inc/process_table.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/points.h>
#include <dataprocess/params.h>
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

    uint8_t value[] = {0xaa, 0x55};
    uint8_t result[2] = {0};
    zassert_ok(dp_set_para(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value));
    zassert_equal(dp_get_para(DEVICE_LOCAL, SIGNAL_DI, sizeof(result), result), sizeof(result));
    zassert_mem_equal(value, result, sizeof(value));

    uint8_t period[2];
    zassert_equal(
        dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, sizeof(period), period), sizeof(period));
    zassert_equal(period[0] | ((uint16_t)period[1] << 8), CONFIG_IDU_SAMPLE_PERIOD_MS);

    dp_param_status_t storage;
    dp_params_get_status(&storage);
    zassert_true(storage.flags & DP_PARAM_STORAGE_READY);

    zassert_equal(protocol_init(), -EEXIST);
}

ZTEST_SUITE(process_runtime, NULL, NULL, NULL, NULL, NULL);
