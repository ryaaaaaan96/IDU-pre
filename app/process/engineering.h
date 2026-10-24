/**
 * @file
 * @brief 产品工程量换算接口。
 */
#ifndef ENGINEERING_H
#define ENGINEERING_H

#include <stdint.h>

/**
 * @brief 将 ADC 原始值换算为通信单位（0.01 A），并按 U16 上限饱和处理。
 */
uint16_t engineering_current_from_raw(uint16_t raw);

#endif
