/** @file @brief 与 DP、Shell 无关的整数文本解析和显示。 */
#ifndef COMMON_VALUE_TEXT_H
#define COMMON_VALUE_TEXT_H

#include <common/value.h>

/** @brief 解析十进制或 0x 十六进制整数并检查边界；失败保持输出不变。 */
int value_text_parse_integer(const char *text, int64_t minimum, int64_t maximum, int64_t *value);

/** @brief 按整数类型严格解析文本，不做低位截断；失败保持输出不变。 */
int value_text_parse(value_type_t type, const char *text, value_t *value);

/** @brief 将整数值格式化为十进制；非整数返回 -ENOTSUP，缓冲不足返回 -ENOSPC。 */
int value_text_format(value_type_t type, const value_t *value, char *text, size_t capacity);

/** @brief 返回类型的英文名称，非法类型返回 unsupported。 */
const char *value_text_type_name(value_type_t type);

#endif
