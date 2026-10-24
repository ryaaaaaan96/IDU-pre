/**
 * @file
 * @brief 模拟量与数字量的组合采样接口。
 *
 * 仅在线程中调用；调用方持有不可变配置与输出缓冲，并串行访问硬件。
 *
 * 模拟量组或数字量组均可省略。输出顺序与配置一致； 任一错误都使 valid 为 false，不得消费部分输出。 容量错误在开始采样前报告。
 */
#ifndef INPUT_SAMPLER_H
#define INPUT_SAMPLER_H

#include <stdbool.h>
#include <analog_input/analog_input.h>
#include <gpio/digital_io.h>

/**
 * @brief 可选模拟量组和数字量组配置。
 */
struct input_sampler_config {
    const struct analog_input_config *analog;
    const struct digital_io_config *digital_groups;
    size_t digital_group_count;
};

/**
 * @brief 调用方持有的组合样本缓冲区和容量。
 */
struct input_sampler_sample {
    uint16_t *analog;
    size_t analog_capacity;
    uint32_t *digital;
    size_t digital_capacity;
    bool valid;
};

/**
 * @brief 校验并初始化配置的模拟量和数字量输入组。
 * @param[in] config 由调用方持有的不可变组合输入配置。
 * @return 成功返回 0，失败返回负错误码。
 * @note 仅在线程中调用，由调用方串行访问硬件。
 */
int input_sampler_init(const struct input_sampler_config *config);
/**
 * @brief 将配置的各组输入读入调用方缓冲区。
 * @param[in] config 不可变输入配置。
 * @param[in,out] sample 调用方提供缓冲区及容量，函数更新采样值和有效标志。
 * @return 成功返回 0，失败返回负错误码。
 * @note 容量错误在采样前报告；其他失败也使 valid 为 false，部分结果不得使用。
 */
int input_sampler_read(const struct input_sampler_config *config,
                       struct input_sampler_sample *sample);

#endif
