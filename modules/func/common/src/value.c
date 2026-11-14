#include <common/value.h>
#include <limits.h>
#include <errno.h>

size_t value_integer_bits(value_type_t type)
{
    switch (type) {
    case VALUE_TYPE_U8:
        return 8;
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
        return 16;
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        return 32;
    default:
        return 0;
    }
}

int64_t value_integer_get(value_type_t type, const value_t *value)
{
    switch (type) {
    case VALUE_TYPE_U8:
        return value->u8;
    case VALUE_TYPE_U16:
        return value->u16;
    case VALUE_TYPE_S16:
        return value->s16;
    case VALUE_TYPE_U32:
        return value->u32;
    case VALUE_TYPE_S32:
        return value->s32;
    default:
        return 0;
    }
}

/* 按补码定义解释窄整数，不使用超范围的有符号转换。 */
value_t value_integer_from_bits(value_type_t type, uint32_t bits)
{
    value_t value = {0};
    switch (type) {
    case VALUE_TYPE_U8:
        value.u8 = (uint8_t)bits;
        break;
    case VALUE_TYPE_U16:
        value.u16 = (uint16_t)bits;
        break;
    case VALUE_TYPE_S16: {
        uint16_t low = (uint16_t)bits;
        value.s16 = (int16_t)(low <= INT16_MAX ? (int32_t)low : (int32_t)low - 65536);
        break;
    }
    case VALUE_TYPE_U32:
        value.u32 = bits;
        break;
    case VALUE_TYPE_S32:
        value.s32 = (int32_t)(bits <= INT32_MAX ? (int64_t)bits : (int64_t)bits - 4294967296LL);
        break;
    default:
        break;
    }
    return value;
}

bool value_can_convert(value_type_t source_type, value_type_t target_type)
{
    if (value_integer_bits(source_type) && value_integer_bits(target_type)) {
        return true;
    }
    switch (source_type) {
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_BYTES:
    case VALUE_TYPE_WORDS:
        return source_type == target_type;
    default:
        return false;
    }
}

int value_convert(value_type_t source_type,
    const value_t *source,
    value_type_t target_type,
    value_t *target)
{
    if (!source || !target) {
        return -EINVAL;
    }
    if (!value_can_convert(source_type, target_type)) {
        return -ENOTSUP;
    }
    if (source_type == target_type) {
        *target = *source;
        return 0;
    }
    int64_t number = value_integer_get(source_type, source);
    *target = value_integer_from_bits(target_type, (uint32_t)number);
    return 0;
}
