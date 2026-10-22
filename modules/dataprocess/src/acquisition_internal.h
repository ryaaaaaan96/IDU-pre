#ifndef DP_ACQUISITION_INTERNAL_H
#define DP_ACQUISITION_INTERNAL_H
#include <dataprocess/points.h>

bool dp_acquired_key(const struct dp_equip_cfg *cfg, struct dp_key key);
bool dp_acquired_sample_current(uint16_t dev, int64_t sampled_at);
#if defined(CONFIG_DP_PARAMS)
void dp_params_invalidate_acquired_device(uint16_t dev, int error, int64_t observed_at);
#endif
#endif
