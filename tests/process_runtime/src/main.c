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
    zassert_ok(dp_set_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value));
    zassert_equal(dp_get_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(result), result),
                  sizeof(result));
    zassert_mem_equal(value, result, sizeof(value));

    struct dp_point_state period;
    zassert_ok(dp_params_read((struct dp_key){DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD},
                              &period));
    zassert_equal(period.value.words[0], CONFIG_IDU_SAMPLE_PERIOD_MS);

    struct dp_param_status storage;
    dp_params_get_status(&storage);
    zassert_true(storage.flags & DP_PARAM_STORAGE_READY);

    dp_stop();
    zassert_not_equal(protocol_init(), 0);
    zassert_not_equal(dp_set_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value), 0);
}

ZTEST_SUITE(process_runtime, NULL, NULL, NULL, NULL, NULL);
