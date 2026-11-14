#include <common/value_text.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>

int value_text_parse_integer(const char *text, int64_t minimum, int64_t maximum, int64_t *value)
{
    if (!text || !value || minimum > maximum) {
        return -EINVAL;
    }
    const char *digits = text;
    if (*digits == '-' || *digits == '+') {
        ++digits;
    }
    const int base = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X') ? 16 : 10;
    char *end;
    errno = 0;
    long long parsed = strtoll(text, &end, base);
    if (end == text || *end) {
        return -EINVAL;
    }
    if (errno == ERANGE || parsed < minimum || parsed > maximum) {
        return -ERANGE;
    }
    *value = parsed;
    return 0;
}

int value_text_parse(value_type_t type, const char *text, value_t *value)
{
    if (!value) {
        return -EINVAL;
    }
    int64_t minimum = 0;
    int64_t maximum;
    switch (type) {
    case VALUE_TYPE_U8:
        maximum = UINT8_MAX;
        break;
    case VALUE_TYPE_U16:
        maximum = UINT16_MAX;
        break;
    case VALUE_TYPE_S16:
        minimum = INT16_MIN;
        maximum = INT16_MAX;
        break;
    case VALUE_TYPE_U32:
        maximum = UINT32_MAX;
        break;
    case VALUE_TYPE_S32:
        minimum = INT32_MIN;
        maximum = INT32_MAX;
        break;
    default:
        return -ENOTSUP;
    }
    int64_t number;
    int err = value_text_parse_integer(text, minimum, maximum, &number);
    if (!err) {
        *value = value_integer_from_bits(type, (uint32_t)number);
    }
    return err;
}

int value_text_format(value_type_t type, const value_t *value, char *text, size_t capacity)
{
    if (!value || !text) {
        return -EINVAL;
    }
    if (!value_integer_bits(type)) {
        return -ENOTSUP;
    }
    /* 当前最大整数类型为 S32，十进制最长 11 字节，另留字符串终止符。 */
    char number[12];
    int length =
        snprintk(number, sizeof(number), "%lld", (long long)value_integer_get(type, value));
    if ((size_t)length >= capacity) {
        return -ENOSPC;
    }
    memcpy(text, number, (size_t)length + 1);
    return 0;
}

const char *value_text_type_name(value_type_t type)
{
    switch (type) {
    case VALUE_TYPE_U8:
        return "u8";
    case VALUE_TYPE_U16:
        return "u16";
    case VALUE_TYPE_S16:
        return "s16";
    case VALUE_TYPE_U32:
        return "u32";
    case VALUE_TYPE_S32:
        return "s32";
    case VALUE_TYPE_STRING:
        return "string";
    case VALUE_TYPE_BYTES:
        return "bytes";
    case VALUE_TYPE_WORDS:
        return "words";
    default:
        return "unsupported";
    }
}
