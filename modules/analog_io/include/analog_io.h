/**
 * @file
 * @brief 模拟量输入的配置和采样接口。
 *
 * 仅在线程中调用；配置在使用期间保持不变。
 *
 * 支持不超过 16 位的无符号单端采样。硬件初始化由调用方串行执行。 读取结果保留配置的通道顺序，平均值向下取整。 失败时此前的输出元素可能已改变，应将整个样本视为无效。
 */
#ifndef ANALOG_IO_H
#define ANALOG_IO_H

#include <zephyr/drivers/adc.h>

/**
 * @brief 模拟量通道和采样平均配置。
 */
struct analog_adc_config {
    const struct adc_dt_spec *channels;
    size_t channel_count;
    uint16_t average_count;
};

/**
 * @brief 校验通道配置并初始化 ADC 通道。
 * @param[in] config 不可变的 ADC 通道与平均次数配置。
 * @return 成功返回 0，失败返回负错误码。
 * @note 仅在线程中调用，由调用方串行执行硬件初始化。
 */
int analog_adc_init(const struct analog_adc_config *config);
/**
 * @brief 按配置顺序读取各通道的平均采样值。
 * @param[in] config 使用期间保持不变的输入配置。
 * @param[out] values 接收采样值的数组；失败时可能已写入部分结果。
 * @param[in] capacity 输出数组可容纳的元素数。
 * @return 成功返回 0，失败返回负错误码。
 * @note 仅在线程中调用；失败时不得消费整批结果。
 */
int analog_adc_read(const struct analog_adc_config *config,
                      uint16_t *values, size_t capacity);

#endif
