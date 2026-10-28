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

/** @brief 命令点写回调；校验命令口令，动作成功后将值清零。 */
int points_parameter_command(const dp_callback_context_t *context);

/** @brief 参数状态读回调。 */
int points_parameter_status(const dp_callback_context_t *context);

#endif
