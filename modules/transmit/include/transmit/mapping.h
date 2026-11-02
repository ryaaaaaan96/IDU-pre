/** @file @brief 通用测点映射配置。 */
#ifndef TRANSMIT_MAPPING_H
#define TRANSMIT_MAPPING_H

#include <stddef.h>
#include <stdint.h>

#include <common/value.h>
#include <common/scale.h>

/** @brief DP 测点与对外数值的通用映射，不包含协议地址和编码规则。 */
typedef struct {
    uint16_t device_id;
    size_t signal_index;
    value_type_t external_type;
    const value_scale_t *scale;    /**< NULL 表示不进行放缩。 */
} tr_mapping_t;

#endif
