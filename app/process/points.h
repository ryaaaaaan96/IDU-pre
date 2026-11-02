/**
 * @file
 * @brief 公共测点和参数业务回调。
 */
#ifndef POINTS_H
#define POINTS_H

#include <stdint.h>
#include <dataprocess/dataprocess.h>

/** @brief 读取应用采样周期。 */
uint16_t points_sample_period_ms(void);

/** @brief 写 1 同步保存待保存参数，成功后命令值归零；命令本身不持久化。 */
int points_save_parameters(const dp_callback_context_t *context);

/**
 * @brief 操作命令：1 保存后冷重启，2 恢复默认值，3 恢复出厂设置；命令本身不持久化。
 * @note 1 先同步保存待保存参数，失败返回错误且不复位；复位延后 200 ms 让响应先发出。
 * @note 3 擦除全部 DP 参数后重启，不执行保存。
 */
int points_operation_command(const dp_callback_context_t *context);

#endif
