#ifndef TEST_POINT_CONNECTIONS_H
#define TEST_POINT_CONNECTIONS_H

#include <collect/collect.h>
#include <collect_lib.h>
#include <dataprocess/acquisition.h>
#include <dataprocess/params.h>
#include <zephyr/ztest.h>

static inline void fixture_connect_points(void)
{
    const struct co_data_callbacks callbacks = {
        .sample = dp_accept_samples,
        .initialize = dp_initialize_acquired_device,
        .offline = dp_invalidate_acquired_device,
    };
    zassert_ok(co_set_data_callbacks(&callbacks));
#if defined(CONFIG_DP_PARAMS)
    zassert_ok(dp_params_set_transport(co_parameter_transport()));
#endif
    zassert_ok(dp_para_reg_set_func(co_dp_fast_set));
}

#endif
