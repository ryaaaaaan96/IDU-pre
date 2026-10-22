#include "inc/protocol.h"
#include <collect/collect.h>
#include <dataprocess/acquisition.h>
#include <dataprocess/params.h>

int idu_connect_point_services(void)
{
    static const struct co_data_callbacks callbacks = {
        .sample = dp_accept_samples,
        .initialize = dp_initialize_acquired_device,
    };
    int err = co_set_data_callbacks(&callbacks);
#if defined(CONFIG_DP_PARAMS)
    if (!err) { err = dp_params_set_transport(co_parameter_transport()); }
#endif
    if (!err) { err = dp_para_reg_set_func(co_dp_fast_set); }
    return err;
}
