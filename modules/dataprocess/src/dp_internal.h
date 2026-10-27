/**
 * @file
 * @brief DP 模块内部共享的最小接口。
 */
#ifndef DP_INTERNAL_H
#define DP_INTERNAL_H

#include <dataprocess/points.h>

int dp_params_configure(uint8_t area_id);
bool dp_params_mounted(void);
int dp_params_write(struct dp_key key, const struct dp_value *value);
size_t dp_point_count(void);
int dp_config_index(struct dp_key key);
const struct dp_point_cfg *dp_config_point(size_t index, struct dp_point_cfg *scratch);
int dp_check_value(const struct dp_point_cfg *point, const struct dp_value *value);

#endif
