/** @file
 * @brief 本地硬件输入初始化和测点更新。
 */
#ifndef ACQUISITION_H
#define ACQUISITION_H

#define SAMPLE_ANALOG_COUNT 8
#define SAMPLE_DI_COUNT 16

int acquisition_inputs_init(void);
/** @brief 读取 DI 位图和各路 AI，按配置同步写入 DP；失败返回错误。 */
int acquisition_cycle(void);

#endif
