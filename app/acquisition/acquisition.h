/**
 * @file
 * @brief 本地输入配置、样本与采样周期接口。
 */
#ifndef ACQUISITION_H
#define ACQUISITION_H

#include "status/status.h"
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

/**
 * @brief startup() 成功后，由应用线程执行一个采样/发布/转发周期。
 *
 * main 负责周期和休眠；故障由 status 记录并执行指定动作。
 */
int acquisition_cycle(void);

#endif
