#include <common/scale.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>

int value_scale_apply(const value_scale_t *scale, bool inverse, int64_t input, int64_t *output)
{
    if (!output) {
        return -EINVAL;
    }
    if (!scale) {
        *output = input;
        return 0;
    }
    if (!scale->numerator || !scale->denominator) {
        return -EINVAL;
    }
    int64_t value = input;
    int64_t multiplier = inverse ? scale->denominator : scale->numerator;
    int64_t divisor = inverse ? scale->numerator : scale->denominator;
    if (inverse && __builtin_sub_overflow(value, (int64_t)scale->offset, &value)) {
        return -ERANGE;
    }
    if (__builtin_mul_overflow(value, multiplier, &value) ||
        (value == INT64_MIN && divisor == -1)) {
        return -ERANGE;
    }
    value /= divisor;
    if (!inverse && __builtin_add_overflow(value, (int64_t)scale->offset, &value)) {
        return -ERANGE;
    }
    *output = value;
    return 0;
}
