#include <modbus_support/registers.h>
#include <zephyr/sys/byteorder.h>

uint16_t mb_register_value(
    value_type_t type, mb_word_order_t order, size_t count, const value_t *value, size_t index)
{
    size_t word = order == MB_HIGH_WORD_FIRST ? index : count - 1 - index;
    if (type == VALUE_TYPE_WORDS) {
        return value->words[word];
    }
    if (type == VALUE_TYPE_BYTES || type == VALUE_TYPE_STRING) {
        return sys_get_be16(value->bytes + word * 2);
    }
    uint32_t bits = (uint32_t)value_integer_get(type, value);
    size_t shift = (count - 1 - word) * 16;
    return (uint16_t)(bits >> shift);
}

value_t mb_decode_value(value_type_t type,
    mb_word_order_t order,
    size_t count,
    const uint8_t *data,
    size_t offset,
    bool bits)
{
    value_t value = {0};
    uint32_t scalar = 0;
    for (size_t i = 0; i < count; ++i) {
        if (bits) {
            scalar |= (uint32_t)((data[(offset + i) / 8] >> ((offset + i) % 8)) & 1U) << i;
            continue;
        }
        uint16_t word = sys_get_be16(data + (offset + i) * 2);
        size_t index = order == MB_HIGH_WORD_FIRST ? i : count - 1 - i;
        if (type == VALUE_TYPE_WORDS) {
            value.words[index] = word;
        } else if (type == VALUE_TYPE_BYTES || type == VALUE_TYPE_STRING) {
            sys_put_be16(word, value.bytes + index * 2);
        } else {
            scalar |= (uint32_t)word << ((count - 1 - index) * 16);
        }
    }
    return value_integer_bits(type) ? value_integer_from_bits(type, scalar) : value;
}
