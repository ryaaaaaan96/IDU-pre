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

/* DP 的参数、类型和范围拒绝统一映射为非法数据值，其他执行错误为从机故障。 */
static int exception_from_access_error(int err)
{
    switch (err) {
    case -ERANGE:
    case -ENOTSUP:
    case -EINVAL:
        return MODBUS_EXC_ILLEGAL_DATA_VAL;
    default:
        return MODBUS_EXC_SERVER_DEVICE_FAILURE;
    }
}

static int preflight(
    const tr_modbus_t *config, mb_data_area_t area, uint32_t first, uint32_t count, bool writing)
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
    const tr_modbus_t *config, mb_data_area_t area, uint16_t first, uint16_t count, uint8_t *data)
{
    bool bits = area == MB_DATA_AREA_COIL || area == MB_DATA_AREA_DISCRETE_INPUT;
    for (uint32_t address = first; address < (uint32_t)first + count;) {
        const tr_modbus_point_t *point = find_point(config, area, address);
        value_t value = {0};
        int err = tr_mapping_read(point, &value);
        if (err) {
            return exception_from_access_error(err);
        }
        uint32_t end = MIN((uint32_t)point->address + point->count, (uint32_t)first + count);
        while (address < end) {
            size_t index = address - point->address;
            size_t position = address - first;
            if (bits) {
                uint32_t bit_value = value_integer_get(point->access.value_type, &value);
                if ((bit_value >> index) & 1U) {
                    data[position / 8] |= BIT(position % 8);
                }
            } else {
                sys_put_be16(
                    mb_register_value(
                        point->access.value_type, point->word_order, point->count, &value, index),
                    data + position * 2);
            }
            ++address;
        }
    }
    return 0;
}

static int write_request(const tr_modbus_t *config,
    mb_data_area_t area,
    uint16_t first,
    uint16_t count,
    const uint8_t *data)
{
    /* 第一遍仅做确定性的协议换算校验；第二遍才允许 DP 回调产生副作用。 */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t address = first; address < (uint32_t)first + count;) {
            const tr_modbus_point_t *point = find_point(config, area, address);
            value_t value = mb_decode_value(point->access.value_type,
                point->word_order,
                point->count,
                data,
                address - first,
                area == MB_DATA_AREA_COIL);
            int err;
            if (pass) {
                err = tr_mapping_write(point, &value);
            } else {
                err = tr_mapping_check_write(point, &value);
            }
            if (err) {
                return exception_from_access_error(err);
            }
            address += point->count;
        }
    }
    return 0;
}

int tr_modbus_validate(const tr_modbus_t *config)
{
    if (!config->unit || config->unit > 247 || !config->gap_us ||
        (!config->point_count && !config->route_count) ||
        (config->point_count && !config->points) || (config->route_count && !config->routes)) {
        return -EINVAL;
    }
    for (size_t i = 0; i < config->route_count; ++i) {
        const tr_modbus_route_t *route = &config->routes[i];
        uint32_t end = (uint32_t)route->address + route->count;
        if (!route->count || end > 65536 || !route->callback) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            const tr_modbus_route_t *other = &config->routes[j];
            if (route->address < (uint32_t)other->address + other->count && other->address < end) {
                return -EEXIST;
            }
        }
        for (size_t j = 0; j < config->point_count; ++j) {
            const tr_modbus_point_t *point = &config->points[j];
            if (route->address < (uint32_t)point->address + point->count && point->address < end) {
                return -EEXIST;
            }
        }
    }
    for (size_t i = 0; i < config->point_count; ++i) {
        const tr_modbus_point_t *point = &config->points[i];
        size_t width = value_integer_bits(point->access.value_type);
        bool bits = point->area == MB_DATA_AREA_COIL || point->area == MB_DATA_AREA_DISCRETE_INPUT;
        if ((point->area != MB_DATA_AREA_COIL && point->area != MB_DATA_AREA_DISCRETE_INPUT &&
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
            if ((point->access.value_type != VALUE_TYPE_WORDS &&
                    point->access.value_type != VALUE_TYPE_BYTES &&
                    point->access.value_type != VALUE_TYPE_STRING) ||
                point->count > VALUE_WORDS) {
                return -EINVAL;
            }
        }
        int err = tr_mapping_validate(point);
        if (err) {
            return err;
        }
        if (bits && point->scale) {
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
        return write_request(config, request->area, request->first, request->count, request->data);
    }
    return read_request(config, request->area, request->first, request->count, request->data);
}

int tr_modbus_execute(mb_server_t *server,
    const tr_modbus_t *config,
    vport_channel_id_t channel_id,
    bool allow_routes,
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
    *response_size = 0;
    if (rx.unit_id && rx.unit_id != config->unit) {
        return 0;
    }
    if (rx.length >= 2) {
        uint16_t address = sys_get_be16(rx.data);
        for (size_t i = 0; i < config->route_count; ++i) {
            const tr_modbus_route_t *route = &config->routes[i];
            if (address < route->address || address - route->address >= route->count) {
                continue;
            }
            tx.unit_id = rx.unit_id;
            tx.fc = rx.fc;
            if (!allow_routes) {
                if (!rx.unit_id) {
                    return 0;
                }
                tx.fc |= 0x80;
                tx.data[0] = MODBUS_EXC_ILLEGAL_DATA_ADDR;
                tx.length = 1;
                return mb_rtu_encode(&tx, response, capacity, response_size);
            }
            err = route->callback(route->context, channel_id, &rx, &tx);
            if (err == TR_MODBUS_ROUTE_SILENT) {
                return 0;
            }
            if (err) {
                return err;
            }
            if (!rx.unit_id) {
                return 0;
            }
            return mb_rtu_encode(&tx, response, capacity, response_size);
        }
    }
    bool respond = false;
    err = mb_server_process(server, &rx, &tx, &respond, access_request, (void *)config);
    *response_size = 0;
    if (err || !respond) {
        return err;
    }
    return mb_rtu_encode(&tx, response, capacity, response_size);
}
