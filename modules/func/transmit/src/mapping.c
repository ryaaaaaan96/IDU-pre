#include "internal.h"
#include <dataprocess/dataprocess.h>
#include <errno.h>

/* 只换算数值，不访问 DP，供整笔写入预检和实际提交共同使用。 */
static int prepare_write(const tr_modbus_point_t *mapping, const value_t *input, value_t *output)
{
    *output = *input;
    if (!mapping->scale) {
        return 0;
    }

    int64_t converted;
    int err = value_scale_apply(
        mapping->scale, true, value_integer_get(mapping->access.value_type, input), &converted);
    if (err) {
        return err;
    }
    *output = value_integer_from_bits(mapping->access.value_type, (uint32_t)converted);
    return 0;
}

int tr_mapping_validate(const tr_modbus_point_t *mapping)
{
    const unsigned int flags = (unsigned int)mapping->access.flags;
    const unsigned int write_flags = DP_ACCESS_FLAG_SET_CALLBACK | DP_ACCESS_FLAG_CHANGE_CALLBACK;
    const bool read_only = mapping->area == MB_DATA_AREA_INPUT_REGISTER ||
                           mapping->area == MB_DATA_AREA_DISCRETE_INPUT;

    if (!mapping->access.device_id || (flags & ~(unsigned int)DP_ACCESS_FLAG_MASK) ||
        (read_only && (flags & write_flags)) ||
        ((flags & DP_ACCESS_FLAG_READ_WRITEBACK) && !(flags & DP_ACCESS_FLAG_GET_CALLBACK))) {
        return -EINVAL;
    }
    if (mapping->scale && (!value_integer_bits(mapping->access.value_type) ||
                              !mapping->scale->numerator || !mapping->scale->denominator)) {
        return -EINVAL;
    }
    return 0;
}

int tr_mapping_read(const tr_modbus_point_t *mapping, value_t *value)
{
    /* 先按对外类型取得 DP 值，可能发生的收窄先于放缩。 */
    value_type_t type = mapping->access.value_type;
    int err = dp_get_value_by_index(&mapping->access, value);
    if (err || !mapping->scale) {
        return err;
    }

    int64_t result;
    err = value_scale_apply(mapping->scale, false, value_integer_get(type, value), &result);
    if (!err) {
        *value = value_integer_from_bits(mapping->access.value_type, (uint32_t)result);
    }
    return err;
}

int tr_mapping_check_write(const tr_modbus_point_t *mapping, const value_t *value)
{
    value_t converted;
    return prepare_write(mapping, value, &converted);
}

int tr_mapping_write(const tr_modbus_point_t *mapping, const value_t *value)
{
    value_t converted;
    int err = prepare_write(mapping, value, &converted);
    if (err) {
        return err;
    }
    return dp_set_value_by_index(&mapping->access, &converted);
}
