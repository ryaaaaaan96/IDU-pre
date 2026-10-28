#include "protocol/inc/protocol.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

ZTEST(process_runtime, test_local_process_without_communication)
{
    zassert_ok(protocol_init());

    uint8_t value[] = {0xaa, 0x55};
    uint8_t result[2] = {0};
    zassert_ok(dp_set_para(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value));
    zassert_equal(dp_get_para(DEVICE_LOCAL, SIGNAL_DI, sizeof(result), result),
                  sizeof(result));
    zassert_mem_equal(value, result, sizeof(value));

    uint8_t period[2];
    zassert_equal(dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, sizeof(period),
                             period), sizeof(period));
    zassert_equal(period[0] | ((uint16_t)period[1] << 8), CONFIG_IDU_SAMPLE_PERIOD_MS);

    struct dp_param_status storage;
    dp_params_get_status(&storage);
    zassert_true(storage.flags & DP_PARAM_STORAGE_READY);

    zassert_equal(protocol_init(), -EALREADY);

}

ZTEST_SUITE(process_runtime, NULL, NULL, NULL, NULL, NULL);
