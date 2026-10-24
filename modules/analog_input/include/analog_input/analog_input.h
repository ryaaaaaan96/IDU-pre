#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

#include <zephyr/drivers/adc.h>

struct analog_input_config {
    const struct adc_dt_spec *channels;
    size_t channel_count;
    uint16_t average_count;
};

/* 仅在线程中调用；配置在使用期间保持不变。
 * 支持不超过 16 位的无符号单端采样。硬件初始化由调用方串行执行。
 * 读取结果保留配置的通道顺序，平均值向下取整。
 * 失败时此前的输出元素可能已改变，应将整个样本视为无效。
 */
int analog_input_init(const struct analog_input_config *config);
int analog_input_read(const struct analog_input_config *config,
                      uint16_t *values, size_t capacity);

#endif
