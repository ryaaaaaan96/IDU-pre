/** @file @brief 产品 DP 回调。 */
#ifndef DP_CALLBACKS_H
#define DP_CALLBACKS_H
#include <dataprocess/dataprocess.h>
int points_fan_pwm(const dp_callback_context_t *context);
int points_fan_type(const dp_callback_context_t *context);
int points_fan_feedback_read(const dp_callback_context_t *context);
int points_unavailable(const dp_callback_context_t *context);
int points_analog_read(const dp_callback_context_t *context);
#endif
