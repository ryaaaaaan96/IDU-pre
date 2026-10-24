/**
 * @file
 * @brief 产品输入配置与样本定义。
 */
#ifndef ACQUISITION_SAMPLE_H
#define ACQUISITION_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 产品样本布局；通用输入模块不限定这些维度。
 */
#define SAMPLE_ANALOG_COUNT 8
#define SAMPLE_DI_COUNT 16

struct acquisition_sample {
    uint16_t di;
    uint16_t raw[SAMPLE_ANALOG_COUNT];
    uint8_t address;
    bool valid;
};

int acquisition_inputs_init(void);
int acquisition_inputs_sample(struct acquisition_sample *sample);

#endif
