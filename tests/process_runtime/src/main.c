#include "protocol/inc/protocol.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <zephyr/ztest.h>
#include <platform_callback.h>
#include <dataprocess/point_parameters.h>

ZTEST(process_runtime, test_local_process_without_communication)
{
    zassert_equal(dp_register_devices(NULL, 1), -EINVAL);
    zassert_equal(dp_register_devices((const DEV_TABLE_S[]){{0}}, 0), -EINVAL);
    zassert_ok(protocol_init());
    zassert_equal(dp_register_devices(dp_config()->reference_devices, 1), -EBUSY);
    zassert_equal(dp_register_config_provider(NULL), -EBUSY);
    zassert_equal(dp_config()->reference_device_count, 1);
    zassert_equal(dp_config()->reference_devices[0].dev_id, DEVICE_LOCAL);
    zassert_equal(dp_config()->device_count, 0);
    zassert_equal(dp_config()->collect_count, 0);
    DM_SIG_DATA_S parameters;
    bool restore;
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON, DEVICE_LOCAL,
                                      SIGNAL_SAMPLE_PERIOD, &parameters, &restore));
    zassert_equal(parameters.def_value.u16, CONFIG_IDU_SAMPLE_PERIOD_MS);
    uint8_t value[] = {0xaa, 0x55};
    uint8_t result[2] = {0};
    zassert_ok(dp_set_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value));
    zassert_equal(dp_get_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(result), result), sizeof(result));
    zassert_mem_equal(value, result, sizeof(value));
    struct dp_param_status storage;
    dp_params_get_status(&storage);
    zassert_false(storage.flags & DP_PARAM_STORAGE_READY);
    zassert_true(dp_get_sig_value(DEVICE_REMOTE_IO, SIGNAL_DI, sizeof(result), result) < 0);
    dp_stop();
    dp_stop();
    zassert_not_equal(protocol_init(), 0);
    zassert_not_equal(dp_set_sig_value(DEVICE_LOCAL, SIGNAL_DI, sizeof(value), value), 0);
}
ZTEST_SUITE(process_runtime, NULL, NULL, NULL, NULL, NULL);
