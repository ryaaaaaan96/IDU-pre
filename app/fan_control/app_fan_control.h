#ifndef APP_FAN_CONTROL_H_
#define APP_FAN_CONTROL_H_
#include <stdint.h>
/** 装配八路固定 10 kHz PWM 输出；反馈轮询由 acquisition 负责。 */
int app_fan_control_init(void);
/** 产品占空比以 0.1% 表示，通道索引 0–7。 */
int app_fan_control_set_duty(uint8_t channel, uint16_t per_mille);
int app_fan_control_get_duty(uint8_t channel, uint16_t *per_mille);
#endif
