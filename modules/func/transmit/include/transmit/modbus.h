/** @file @brief Modbus 转发表与从机配置。 */
#ifndef TRANSMIT_MODBUS_H
#define TRANSMIT_MODBUS_H

#include <vport/vport.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <dataprocess/dataprocess.h>
#include <common/scale.h>
#include <modbus_support/server.h>
#include <modbus_support/registers.h>

/** @brief 一个完整测点的 Modbus 地址与编码配置。 */
typedef struct {
    mb_data_area_t area;
    uint16_t address;
    uint16_t count;
    mb_word_order_t word_order;
    dp_index_access_t access;
    const value_scale_t *scale; /**< NULL 表示不进行放缩。 */
} tr_modbus_point_t;

/** @brief 地址段已消费但不回复。 */
#define TR_MODBUS_ROUTE_SILENT (-ECANCELED)

/**
 * @brief 完整 ADU 地址段回调；返回 0 回复，SILENT 静默，其他负值表示处理失败。
 * @note 只按请求前两个数据字节匹配起始地址，功能码及长度由回调校验。
 *       已匹配请求不回退点表。channel_id 表示来源通道；禁止路由时不调用回调。
 *       允许路由时广播可进入回调，但统一禁止响应。
 *       data 保留协议字节序；请求、响应仅在同步调用期间有效。
 */
typedef int (*tr_modbus_route_callback_t)(void *context,
    vport_channel_id_t channel_id,
    const struct modbus_adu *request,
    struct modbus_adu *response);

typedef struct {
    uint16_t address;
    uint16_t count;
    tr_modbus_route_callback_t callback;
    void *context;
} tr_modbus_route_t;

typedef struct {
    uint8_t unit;
    uint32_t gap_us;
    const tr_modbus_point_t *points;
    size_t point_count;
    const tr_modbus_route_t *routes;
    size_t route_count;
} tr_modbus_t;

#endif
