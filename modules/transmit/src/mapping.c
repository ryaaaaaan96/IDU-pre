#include "internal.h"
#include <dataprocess/dataprocess.h>
#include <errno.h>

/* 只换算数值，不访问 DP，供整笔写入预检和实际提交共同使用。 */
static int prepare_write(
    const tr_mapping_t *mapping, const value_t *input, value_t *output)
{
    *output = *input;
    if (!mapping->scale) {
        return 0;
    }

    int64_t converted;
    int err = value_scale_apply(
        mapping->scale, true, value_integer_get(mapping->external_type, input), &converted);
    if (err) {
        return err;
    }
    *output = value_integer_from_bits(mapping->external_type, (uint32_t)converted);
    return 0;
}

int tr_mapping_validate(const tr_mapping_t *mapping)
{
    if (!mapping->device_id) {
        return -EINVAL;
    }
    if (mapping->scale &&
        (!value_integer_bits(mapping->external_type) || !mapping->scale->numerator ||
            !mapping->scale->denominator)) {
        return -EINVAL;
    }
    return 0;
}

int tr_mapping_read(const tr_mapping_t *mapping, value_t *value)
{
    /* 先按对外类型取得 DP 值，可能发生的收窄先于放缩。 */
    value_type_t type = mapping->external_type;
    int err = dp_get_para_by_index(mapping->device_id, mapping->signal_index, type, value);
    if (err || !mapping->scale) {
        return err;
    }

    int64_t result;
    err = value_scale_apply(mapping->scale, false, value_integer_get(type, value), &result);
    if (!err) {
        *value = value_integer_from_bits(mapping->external_type, (uint32_t)result);
    }
    return err;
}

int tr_mapping_check_write(const tr_mapping_t *mapping, const value_t *value)
{
    value_t converted;
    return prepare_write(mapping, value, &converted);
}

int tr_mapping_write(const tr_mapping_t *mapping, const value_t *value)
{
    value_t converted;
    int err = prepare_write(mapping, value, &converted);
    if (err) {
        return err;
    }
    return dp_set_para_by_index(
        mapping->device_id, mapping->signal_index, mapping->external_type, &converted);
}
