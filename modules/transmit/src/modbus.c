#include "internal.h"
#include <modbus_support/rtu.h>
#include <modbus_support/registers.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>

static const tr_modbus_point_t *find_point(
    const tr_modbus_t *config, mb_data_area_t area, uint32_t address)
{
    for (size_t i = 0; i < config->point_count; ++i) {
        const tr_modbus_point_t *point = &config->points[i];
        if (point->area == area && address >= point->address &&
            address - point->address < point->count) {
            return point;
        }
    }
    return NULL;
}

static int preflight(
    const tr_modbus_t *config,
    mb_data_area_t area,
    uint32_t first,
    uint32_t count,
    bool writing)
{
    uint32_t end = first + count;
    for (uint32_t address = first; address < end;) {
        const tr_modbus_point_t *point = find_point(config, area, address);
        if (!point) {
            return MODBUS_EXC_ILLEGAL_DATA_ADDR;
        }
        uint32_t point_end = (uint32_t)point->address + point->count;
        if (writing && (address != point->address || point_end > end)) {
            return MODBUS_EXC_ILLEGAL_DATA_ADDR;
        }
        address = MIN(point_end, end);
    }
    return 0;
}

static int read_request(
    const tr_modbus_t *config,
    mb_data_area_t area,
    uint16_t first,
    uint16_t count,
    uint8_t *data)
{
    bool bits = area == MB_DATA_AREA_COIL || area == MB_DATA_AREA_DISCRETE_INPUT;
    for (uint32_t address = first; address < (uint32_t)first + count;) {
        const tr_modbus_point_t *point = find_point(config, area, address);
        value_t value = {0};
        int err = tr_mapping_read(&point->mapping, &value);
        if (err) {
            return err == -ERANGE || err == -ENOTSUP ? MODBUS_EXC_ILLEGAL_DATA_VAL
                                                     : MODBUS_EXC_SERVER_DEVICE_FAILURE;
        }
        uint32_t end = MIN((uint32_t)point->address + point->count, (uint32_t)first + count);
        while (address < end) {
            size_t index = address - point->address;
            size_t position = address - first;
            if (bits) {
                uint32_t bit_value = value_integer_get(point->mapping.external_type, &value);
                if ((bit_value >> index) & 1U) {
                    data[position / 8] |= BIT(position % 8);
                }
            } else {
                sys_put_be16(mb_register_value(
                                 point->mapping.external_type,
                                 point->word_order,
                                 point->count,
                                 &value,
                                 index),
                    data + position * 2);
            }
            ++address;
        }
    }
    return 0;
}

static int write_request(
    const tr_modbus_t *config,
    mb_data_area_t area,
    uint16_t first,
    uint16_t count,
    const uint8_t *data)
{
    /* 第一遍仅做确定性的协议换算校验；第二遍才允许 DP 回调产生副作用。 */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t address = first; address < (uint32_t)first + count;) {
            const tr_modbus_point_t *point = find_point(config, area, address);
            value_t value = mb_decode_value(point->mapping.external_type,
                point->word_order,
                point->count,
                data,
                address - first,
                area == MB_DATA_AREA_COIL);
            int err;
            if (pass) {
                err = tr_mapping_write(&point->mapping, &value);
            } else {
                err = tr_mapping_check_write(&point->mapping, &value);
            }
            if (err) {
                return err == -ERANGE || err == -ENOTSUP || err == -EINVAL
                           ? MODBUS_EXC_ILLEGAL_DATA_VAL
                           : MODBUS_EXC_SERVER_DEVICE_FAILURE;
            }
            address += point->count;
        }
    }
    return 0;
}

int tr_modbus_validate(const tr_modbus_t *config)
{
    if (!config->unit || config->unit > 247 || !config->gap_us || !config->points ||
        !config->point_count) {
        return -EINVAL;
    }
    for (size_t i = 0; i < config->point_count; ++i) {
        const tr_modbus_point_t *point = &config->points[i];
        size_t width = value_integer_bits(point->mapping.external_type);
        bool bits = point->area == MB_DATA_AREA_COIL ||
                    point->area == MB_DATA_AREA_DISCRETE_INPUT;
        if ((point->area != MB_DATA_AREA_COIL &&
                point->area != MB_DATA_AREA_DISCRETE_INPUT &&
                point->area != MB_DATA_AREA_HOLDING_REGISTER &&
                point->area != MB_DATA_AREA_INPUT_REGISTER) ||
            !point->count || (uint32_t)point->address + point->count > 65536 ||
            (point->word_order != MB_HIGH_WORD_FIRST && point->word_order != MB_LOW_WORD_FIRST)) {
            return -EINVAL;
        }
        if (bits) {
            if (!width || point->count > width) {
                return -EINVAL;
            }
        } else if (width) {
            if (point->count != DIV_ROUND_UP(width, 16)) {
                return -EINVAL;
            }
        } else {
            if ((point->mapping.external_type != VALUE_TYPE_WORDS &&
                    point->mapping.external_type != VALUE_TYPE_BYTES &&
                    point->mapping.external_type != VALUE_TYPE_STRING) ||
                point->count > VALUE_WORDS) {
                return -EINVAL;
            }
        }
        int err = tr_mapping_validate(&point->mapping);
        if (err) {
            return err;
        }
        if (bits && point->mapping.scale) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            const tr_modbus_point_t *other = &config->points[j];
            if (point->area == other->area &&
                point->address < (uint32_t)other->address + other->count &&
                other->address < (uint32_t)point->address + point->count) {
                return -EEXIST;
            }
        }
    }
    return 0;
}

static int access_request(void *context, const mb_request_t *request)
{
    const tr_modbus_t *config = context;
    int exception =
        preflight(config, request->area, request->first, request->count, request->writing);
    if (exception) {
        return exception;
    }
    if (request->writing) {
        return write_request(
            config, request->area, request->first, request->count, request->data);
    }
    return read_request(
        config, request->area, request->first, request->count, request->data);
}

int tr_modbus_execute(mb_server_t *server,
    const tr_modbus_t *config,
    const uint8_t *request,
    size_t size,
    uint8_t *response,
    size_t capacity,
    size_t *response_size)
{
    struct modbus_adu rx;
    struct modbus_adu tx = {0};
    int err = mb_rtu_decode(request, size, &rx);
    if (err) {
        return err;
    }
    bool respond = false;
    err = mb_server_process(server, &rx, &tx, &respond, access_request, (void *)config);
    *response_size = 0;
    if (err || !respond) {
        return err;
    }
    return mb_rtu_encode(&tx, response, capacity, response_size);
}
