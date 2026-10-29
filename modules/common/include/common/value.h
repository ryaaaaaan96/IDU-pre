/** @file @brief 各模块共享的数值存储类型，不依赖业务模块。 */
#ifndef COMMON_VALUE_H
#define COMMON_VALUE_H

#include <stdint.h>

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
