#ifndef POINTS_H
#define POINTS_H

#include <stdint.h>
/* 采样映射和业务回调；整机启动见 main.c，协议执行归通用模块。 */
typedef struct func_para_all FUNC_PARA_ALL;

struct acquisition_sample;

/* 应用线程发布样本和读取采样周期。 */
int points_publish_snapshot(const struct acquisition_sample *snapshot);
uint16_t points_sample_period_ms(void);

/* 测点回调，由处理点表注册。 */
int32_t points_parameter_command(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                               void *new_value, void *old_value);
int32_t points_parameter_status(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t points_default_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value);
int32_t points_initialize_callback(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);

#endif
