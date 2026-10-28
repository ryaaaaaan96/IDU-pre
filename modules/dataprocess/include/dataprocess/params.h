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

typedef struct {
    uint32_t sequence;
    int error;
    uint16_t flags;
    int storage_error;
} dp_param_status_t;

int dp_params_restore(uint16_t dev_id);
void dp_params_get_status(dp_param_status_t *status);

#endif
