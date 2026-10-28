/** @file @brief 点表登记、同步点值读写。 */
#ifndef DATAPROCESS_H
#define DATAPROCESS_H

#include <common/value.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef enum {
    DP_POINT_FLAG_SKIP_RESTORE = (1U << 5), /**< 显式恢复默认时跳过，不影响上电设置初值。 */
    DP_POINT_FLAG_CHECK_RANGE = (1U << 6), /**< 按配置上下限校验。 */
} dp_point_flags_t;

typedef struct {
    value_t value;
    uint32_t updated_at_s; /**< UNIX 秒，UTC；未校时和配置初值为 0。 */
} dp_point_data_t;

/** @brief 同步回调上下文；身份只读，value 可修改，所有指针仅在回调期间有效。 */
typedef struct {
    uint16_t device_id;
    uint16_t signal_key;
    bool is_write; /**< true 为写回调，false 为读回调。 */
    size_t signal_index; /**< 注册表内从零开始的行下标，对应应用的 process 枚举。 */
    value_t *value;
} dp_callback_context_t;

/** @brief 返回 0 表示成功，负值表示失败；不得保存上下文或 value 指针。 */
typedef int (*dp_callback_t)(const dp_callback_context_t *context);

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
    dp_callback_t write_callback;
    dp_callback_t read_callback;
    dp_point_limits_t data;
} dp_point_config_t;

typedef struct {
    const dp_point_config_t *points;
    size_t point_count;
} dp_table_t;


/**
 * @brief 启动线程逐张登记设备点表；描述可临时，行数组及其引用数据必须常驻只读，不得修改或释放。
 * @note 同一张表可登记给多个设备；设备 ID 必须非零且唯一，运行数据由 DP 独立分配。
 * @note 登记成功即已设置配置初值；点值仅保存在 RAM，重启后恢复初值。
 * @note 登记须串行且先于业务线程，期间禁止并发读写。
 * @retval -ENOMEM 私有登记节点和点数据分配失败。
 */
int dp_register_table(uint16_t device_id, const dp_table_t *table);

/** @brief 同步写点；长度须匹配，回调后校验最终值并保存，仅更新 RAM。 */
int dp_set_para(uint16_t device_id, uint16_t signal_key, uint8_t size, const uint8_t *bytes);

/** @brief 同步读点，可执行读回调；成功返回传入容量，数据按低字节在前编码。 */
int dp_get_para(uint16_t device_id, uint16_t signal_key, uint8_t capacity, uint8_t *bytes);

/**
 * @brief 将指定设备的点值恢复为配置初值，跳过 SKIP_RESTORE 点，不调用业务回调。
 * @note 仅更新 RAM，恢复点的时间随之更新；被跳过的点保留值及时间。
 */
int dp_restore_defaults(uint16_t device_id);

#endif
