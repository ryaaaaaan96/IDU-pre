/** @file @brief 各模块共享的数值存储类型，不依赖业务模块。 */
#ifndef COMMON_VALUE_H
#define COMMON_VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VALUE_WORDS CONFIG_COMMON_VALUE_WORDS

typedef enum {
    VALUE_TYPE_U8,
    VALUE_TYPE_U16,
    VALUE_TYPE_S16,
    VALUE_TYPE_U32,
    VALUE_TYPE_S32,
    VALUE_TYPE_STRING,
    VALUE_TYPE_BYTES,
    VALUE_TYPE_WORDS,
} value_type_t;

/**
 * @brief 数值访问视图；类型、有效长度和所有权由使用方约定。
 * @note array 是外部只读数据引用，与 bytes/words 内嵌数据互斥。
 * @note 本类型不定义序列化格式，也不负责数值转换。
 */
typedef union {
    uint8_t bytes[VALUE_WORDS * 2];
    uint16_t words[VALUE_WORDS];
    uint8_t u8;
    uint16_t u16;
    int16_t s16;
    uint32_t u32;
    int32_t s32;
    float f32;
    const void *array;
} value_t;

/** @brief 整数位宽；非整数类型返回 0。 */
size_t value_integer_bits(value_type_t type);

/** @brief 按类型取得整数；整数类型要求 value 非空，非整数类型返回 0。 */
int64_t value_integer_get(value_type_t type, const value_t *value);

/** @brief 取低位构造整数，有符号按补码解释；非整数类型返回全零值。 */
value_t value_integer_from_bits(value_type_t type, uint32_t bits);

/** @brief 整数类型之间可互转；STRING/BYTES/WORDS 仅允许同类型复制。 */
bool value_can_convert(value_type_t source_type, value_type_t target_type);

/**
 * @brief 转换本地数值；同类型复制，整数收窄保留低位，有符号按补码解释。
 * @note 支持原地转换；失败不修改输出；不处理协议字序、放缩或有效数组长度。
 * @retval -EINVAL 输入或输出指针为空。
 * @retval -ENOTSUP 类型无效或转换组合不支持。
 */
int value_convert(value_type_t source_type,
    const value_t *source,
    value_type_t target_type,
    value_t *target);

/* 固定存储类型编码，不依赖公共枚举的数值和 C 结构体布局。 */
static inline uint8_t value_storage_type(value_type_t type)
{
    switch (type) {
    case VALUE_TYPE_U8:
        return 1;
    case VALUE_TYPE_U16:
        return 2;
    case VALUE_TYPE_S16:
        return 3;
    case VALUE_TYPE_U32:
        return 4;
    case VALUE_TYPE_S32:
        return 5;
    case VALUE_TYPE_STRING:
        return 6;
    case VALUE_TYPE_BYTES:
        return 7;
    case VALUE_TYPE_WORDS:
        return 8;
    default:
        return 0;
    }
}

#endif
