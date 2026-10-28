/** @file @brief 点表登记、同步点值读写。 */
#ifndef DP_POINTS_H
#define DP_POINTS_H

#include <common/value.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DP_POINT_WORDS VALUE_WORDS

typedef enum {
    DP_POINT_FLAG_PERSISTENT = (1U << 0), /**< 参数同步持久化。 */
    DP_POINT_FLAG_SKIP_RESTORE = (1U << 5), /**< 仅显式恢复默认时跳过，不影响上电恢复 NVS。 */
    DP_POINT_FLAG_CHECK_RANGE = (1U << 6), /**< 按配置上下限校验。 */
} dp_point_flags_t;

typedef struct {
    value_t value;
    uint32_t updated_at_s; /**< UNIX 秒，UTC；未校时、默认值和历史恢复值为 0。 */
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

#define DP_POINT_DEFAULT_VALUE 0
#define DP_POINT_DEFAULT_FLAGS 0U
#define DP_POINT_DEFAULT_CALLBACK NULL
#define DP_POINT_DEFAULT_ARRAY NULL

/**
 * @brief 初始化参数存储：挂载分区并恢复提交日志。
 * @note 必须先于任何点表登记，初始化期间禁止并发调用其他 DP 接口。
 */
int dp_init(void);

/**
 * @brief 启动线程逐张登记设备点表；描述可临时，行数组及其引用数据必须常驻只读，不得修改或释放。
 * @note 同一张表可登记给多个设备；设备 ID 必须非零且唯一，运行数据由 DP 独立分配。
 * @note 先调用 dp_init()；每表登记成功即已加载历史参数。
 * @note 登记须串行且先于业务线程，期间禁止并发读写及状态查询。
 * @retval -ENOMEM 私有登记节点和点数据分配失败。
 */
int dp_register_table(uint16_t device_id, const dp_table_t *table);

/** @brief 同步写点；长度须匹配，回调后校验最终值并保存，参数同步提交 NVS。 */
int dp_set_para(uint16_t device_id, uint16_t signal_key, uint8_t size, const uint8_t *bytes);

/** @brief 同步读点，可执行读回调；成功返回传入容量，数据按低字节在前编码。 */
int dp_get_para(uint16_t device_id, uint16_t signal_key, uint8_t capacity, uint8_t *bytes);

#endif
