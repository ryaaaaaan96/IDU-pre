/** @file @brief 协议无关的整数比例及偏移换算。 */
#ifndef COMMON_SCALE_H
#define COMMON_SCALE_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 正向 output = input * numerator / denominator + offset；反向先去偏移。 */
typedef struct {
    int32_t numerator;
    int32_t denominator;
    int32_t offset;
} value_scale_t;

/**
 * @brief 整数除法向零舍入；失败不修改输出，NULL 配置按恒等处理。
 * @note 非空配置的分子分母必须非零；中间算术溢出返回 -ERANGE，不截断中间结果。
 */
int value_scale_apply(const value_scale_t *scale, bool inverse, int64_t input, int64_t *output);
#endif
