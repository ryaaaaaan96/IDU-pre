#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <stdint.h>
/* 采样映射和业务回调；整机启动见 main.c，协议执行归通用模块。 */
typedef struct func_para_all FUNC_PARA_ALL;

struct idu_snapshot;

/* 应用线程发布样本和读取采样周期。 */
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);

/* IDU 测点回调，由处理点表注册。 */
int32_t idu_parameter_command(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                               void *new_value, void *old_value);
int32_t idu_parameter_status(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t idu_default_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value);
int32_t idu_initialize_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);

#endif
