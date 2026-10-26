/**
 * @file
 * @brief 公共测点和参数业务回调。
 *
 * 硬件输入映射见 acquisition.c，整机启动见 main.c，协议执行归通用模块。
 */
#ifndef POINTS_H
#define POINTS_H

#include <stdint.h>

typedef struct func_para_all FUNC_PARA_ALL;

/**
 * @brief 读取应用采样周期。
 */
uint16_t points_sample_period_ms(void);

/**
 * @brief 测点回调，由处理点表注册。
 */
int32_t points_parameter_command(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                               void *new_value, void *old_value);
int32_t points_parameter_status(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t points_default_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value);
int32_t points_initialize_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);

#endif
