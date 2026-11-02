/** @file @brief Modbus 转发表与从机配置。 */
#ifndef TRANSMIT_MODBUS_H
#define TRANSMIT_MODBUS_H

#include <stddef.h>
#include <stdint.h>

#include <transmit/mapping.h>
#include <modbus_support/server.h>
#include <modbus_support/registers.h>

/** @brief 一个完整测点的 Modbus 地址与编码配置。 */
typedef struct {
    mb_data_area_t area;
    uint16_t address;
    uint16_t count;
    mb_word_order_t word_order;
    tr_mapping_t mapping;
} tr_modbus_point_t;

typedef struct {
    uint8_t unit;
    uint32_t gap_us;
    const tr_modbus_point_t *points;
    size_t point_count;
} tr_modbus_t;

#endif
