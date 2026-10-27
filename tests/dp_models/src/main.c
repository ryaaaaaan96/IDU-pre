#include <dataprocess/model.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <dataprocess/point_parameters.h>
#include <zephyr/ztest.h>

/* 两个设备共用只读定义，各自持有实时值；不启动任何 DP 线程。 */
static const DM_SIG_CFG_S signals[] = {
    {
        .sig_id = 1, .sig_size = 2, .sig_flag = FLAG_RANGE_CHECK,
        .data_type = DATA_TYPE_U16, .pf_sig_proc = NULL,
        .data = {.def_value.u16 = 5, .min_value.u16 = 0, .max_value.u16 = 100},
    },
};
static const struct dp_model_cfg models[] = {
    {.device_id = 10, .points = signals, .point_count = ARRAY_SIZE(signals)},
    {.device_id = 1000, .points = signals, .point_count = ARRAY_SIZE(signals)},
};

ZTEST(dp_models, test_shared_definition_independent_values)
{
    zassert_equal(dp_register_models(NULL, 1), -EINVAL);
    zassert_equal(dp_register_models(models, 0), -EINVAL);
    struct dp_model_cfg bad[] = {models[0], models[0]};
    zassert_equal(dp_register_models(bad, 2), -EEXIST);
    bad[0].points = NULL;
    zassert_equal(dp_register_models(bad, 1), -EINVAL);
    DM_SIG_CFG_S remote = signals[0];
    remote.sig_flag = FLAG_COLLECT;
    bad[0] = (struct dp_model_cfg){.device_id = 1, .points = &remote, .point_count = 1};
    zassert_equal(dp_register_models(bad, 1), -ENOTSUP);

    zassert_ok(dp_register_models(models, ARRAY_SIZE(models)));
    zassert_equal(dp_register_models(models, ARRAY_SIZE(models)), -EBUSY);
    zassert_ok(dp_init());
    zassert_equal(dp_config()->model_count, 2);
    zassert_is_null(dp_config()->reference_devices);
    zassert_equal(dp_config()->collect_count, 0);
    zassert_equal(dp_config()->transmit_count, 0);
    const struct dp_process_plan plan = {.initialize_all = true};
    zassert_ok(dp_process_init(&plan));
    uint8_t value[] = {42, 0};
    uint8_t result[2];
    zassert_ok(dp_set_sig_value(10, 1, sizeof(value), value));
    zassert_equal(dp_get_sig_value(10, 1, sizeof(result), result), sizeof(result));
    zassert_equal(result[0], 42);
    zassert_equal(dp_get_sig_value(1000, 1, sizeof(result), result), sizeof(result));
    /* 共用定义不共用实时值。 */
    zassert_equal(result[0], 5);
    value[0] = 101;
    zassert_true(dp_set_sig_value(10, 1, sizeof(value), value) < 0);
    zassert_equal(dp_get_sig_value(10, 1, sizeof(result), result), sizeof(result));
    zassert_equal(result[0], 42);
    DM_SIG_DATA_S parameters;
    bool restore;
    zassert_ok(dp_point_parameters_get(DP_COUNTRY_COMMON, 1000, 1, &parameters, &restore));
    zassert_equal(parameters.def_value.u16, 5);
}
ZTEST_SUITE(dp_models, NULL, NULL, NULL, NULL, NULL);
