#ifndef INPUT_SAMPLER_H
#define INPUT_SAMPLER_H

#include <stdbool.h>
#include <analog_input/analog_input.h>
#include <gpio/digital_io.h>

struct input_sampler_config {
    const struct analog_input_config *analog;
    const struct digital_io_config *digital_groups;
    size_t digital_group_count;
};

struct input_sampler_sample {
    uint16_t *analog;
    size_t analog_capacity;
    uint32_t *digital;
    size_t digital_capacity;
    bool valid;
};

/* 仅在线程中调用；调用方持有不可变配置与输出缓冲，并串行访问硬件。
 * 模拟量组或数字量组均可省略。输出顺序与配置一致；
 * 任一错误都使 valid 为 false，不得消费部分输出。
 * 容量错误在开始采样前报告。
 */
int input_sampler_init(const struct input_sampler_config *config);
int input_sampler_read(const struct input_sampler_config *config,
                       struct input_sampler_sample *sample);

#endif
