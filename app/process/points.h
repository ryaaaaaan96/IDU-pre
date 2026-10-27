/**
 * @file
 * @brief 公共测点和参数业务回调。
 */
#ifndef POINTS_H
#define POINTS_H

#include <stdint.h>
#include <dataprocess/points.h>

/** @brief 读取应用采样周期。 */
uint16_t points_sample_period_ms(void);

/** @brief 命令点写回调；参数动作只在数值校验通过后执行。 */
int points_parameter_command(struct dp_key key, const struct dp_value *value);

/** @brief 参数状态读回调。 */
int points_parameter_status(struct dp_key key, struct dp_point_state *state);

#endif
