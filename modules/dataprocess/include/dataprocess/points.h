/** @file @brief 点表登记、同步点值读写。 */
#ifndef DP_POINTS_H
#define DP_POINTS_H

#include <common/value.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DP_POINT_WORDS VALUE_WORDS

typedef enum {
    FLAG_PERSISTENCE = (1U << 0),
    FLAG_READ_CALLBACK = (1U << 3),
    FLAG_NO_RESTORE = (1U << 5),
    FLAG_RANGE_CHECK = (1U << 6),
    FLAG_CONSTANT = (1U << 7),
} dp_flags_t;

typedef struct {
    value_t value;
    uint32_t updated_at_s; /**< UNIX 秒，UTC；未校时、默认值和历史恢复值为 0。 */
} dp_point_data_t;

typedef int (*dp_write_callback_t)(uint16_t device_id, uint16_t signal_key, const value_t *value);
typedef int (*dp_read_callback_t)(uint16_t device_id, uint16_t signal_key, value_t *value);

typedef struct {
    value_t initial;
    value_t minimum;
    value_t maximum;
} dp_point_limits_t;

typedef struct {
    uint16_t signal_key;
    uint8_t size;
    uint8_t flags;
    value_type_t type;
    dp_write_callback_t write_callback;
    dp_read_callback_t read_callback;
    dp_point_limits_t data;
} dp_point_config_t;

typedef struct {
    const dp_point_config_t *points;
    size_t point_count;
} dp_table_t;

#define DP_POINT_DEFAULT_VALUE 0
#define DP_POINT_DEFAULT_FLAGS 0U
#define DP_POINT_DEFAULT_CALLBACK NULL
#define DP_POINT_DEFAULT_ARRAY NULL

/**
 * @brief 启动线程逐张登记设备点表；描述可临时，行数组及其引用数据必须常驻只读。
 * @note 同一张表可登记给多个设备；设备 ID 必须非零且唯一，运行数据由 DP 独立分配。
 * @note 完成全部登记后再读写；参数挂载后不能增加持久参数。失败不发布设备。
 * @retval -ENOMEM 私有登记节点和点数据分配失败。
 */
int dp_register_table(uint16_t device_id, const dp_table_t *table);

/** @brief 同步写点；长度须匹配，常量拒写，命令仅执行回调，参数同步提交 NVS。 */
int dp_set_para(uint16_t device_id, uint16_t signal_key, uint8_t size, const uint8_t *bytes);

/** @brief 同步读点，可执行读回调；成功返回传入容量，数据按低字节在前编码。 */
int dp_get_para(uint16_t device_id, uint16_t signal_key, uint8_t capacity, uint8_t *bytes);

#endif
