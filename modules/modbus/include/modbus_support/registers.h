/** @file @brief 寄存器与公共数值的转换，不依赖转发表或 DP。 */
#ifndef MODBUS_SUPPORT_REGISTERS_H
#define MODBUS_SUPPORT_REGISTERS_H
#include <common/value.h>
#include <stdbool.h>

typedef enum {
    MB_HIGH_WORD_FIRST,
    MB_LOW_WORD_FIRST,
} mb_word_order_t;

/** @brief 从已校验的完整测点取得一个寄存器；index 必须小于 count。 */
uint16_t mb_register_value(
    value_type_t type, mb_word_order_t order, size_t count, const value_t *value, size_t index);
/** @brief 解码一个完整测点；调用方保证数据覆盖 offset 及 count，类型长度已校验。 */
value_t mb_decode_value(value_type_t type,
    mb_word_order_t order,
    size_t count,
    const uint8_t *data,
    size_t offset,
    bool bits);
#endif
