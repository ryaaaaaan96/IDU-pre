/**
 * @file
 * @brief 本机参数的 NVS 缓存与持久化。
 */
#ifndef DP_PARAMS_H
#define DP_PARAMS_H

#include <dataprocess/points.h>
#include <zephyr/sys/util.h>

#define DP_PARAM_STORAGE_READY BIT(0)
#define DP_PARAM_HISTORY_RECOVERED BIT(1)
#define DP_PARAM_STORAGE_RESET BIT(2)

struct dp_param_status {
    uint32_t sequence;
    int error;
    uint16_t flags;
    int storage_error;
};

int dp_params_restore(uint16_t dev_id);
void dp_params_get_status(struct dp_param_status *status);

#endif
