/**
 * @file
 * @brief 点表登记与显式类型的同步读写。
 * @note 整数类型可互转，缩窄保留低位；数组类仅允许同类型，不解释协议放缩和字序。
 * @note 不支持的类型组合在业务回调前返回 -ENOTSUP；业务回调始终使用点表内部类型。
 */
#ifndef DATAPROCESS_H
#define DATAPROCESS_H

#include <common/value.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 仅供 dp_factory_reset 使用：清空全部设备的 DP 参数总区域。 */
#define DP_DEVICE_ID_ALL 0U

typedef enum {
    DP_POINT_FLAG_PERSISTENT = (1U << 0),    /**< 延后保存，启动登记时加载历史值。 */
    DP_POINT_FLAG_SKIP_DEFAULTS = (1U << 5), /**< 显式恢复默认时跳过，不影响上电设置初值。 */
    DP_POINT_FLAG_CHECK_RANGE = (1U << 6),   /**< 按配置上下限校验。 */
} dp_point_flags_t;

/** @brief 本次访问的可选行为；范围和持久化属性仍由点表决定。 */
typedef enum {
    DP_ACCESS_FLAG_NONE = 0U,
    DP_ACCESS_FLAG_GET_CALLBACK = (1U << 0),
    DP_ACCESS_FLAG_SET_CALLBACK = (1U << 1),
    DP_ACCESS_FLAG_READ_WRITEBACK = (1U << 2),
    DP_ACCESS_FLAG_CHANGE_CALLBACK = (1U << 3),
} dp_access_flags_t;

/** @brief 当前版本定义的全部 access flag；用于配置登记时拒绝未知位。 */
#define DP_ACCESS_FLAG_MASK                                                                        \
    (DP_ACCESS_FLAG_GET_CALLBACK | DP_ACCESS_FLAG_SET_CALLBACK | DP_ACCESS_FLAG_READ_WRITEBACK |   \
        DP_ACCESS_FLAG_CHANGE_CALLBACK)

typedef struct {
    uint16_t device_id;
    size_t signal_index;
    value_type_t value_type;
    dp_access_flags_t flags;
} dp_index_access_t;

typedef struct {
    uint16_t device_id;
    uint16_t signal_key;
    value_type_t value_type;
    dp_access_flags_t flags;
} dp_key_access_t;

typedef enum {
    DP_CALLBACK_EVENT_GET = (1U << 0),
    DP_CALLBACK_EVENT_SET = (1U << 1),
    DP_CALLBACK_EVENT_CHANGE = (1U << 2),
} dp_callback_events_t;

/**
 * @brief 回调事件集合；每次访问最多调用一次 access_callback。
 * @note get 只产生 GET，READ_WRITEBACK 控制写回，不产生 CHANGE；SET/CHANGE 标志在读取时忽略。
 * @note set 仅处理 SET/CHANGE 标志；SET 每次写入命中，CHANGE 仅在回调前候选值不同于
 *       RAM 快照时命中；两者同时命中时 events 为 SET | CHANGE，仅调用一次。
 * @note CHANGE 描述进入回调前的变化；回调修改候选值后不重新分发事件。
 * @note 任一回调可修改或拒绝候选值；最终校验后，同值不写回、不更新时间或 dirty。
 * @note 回调在锁外执行，旧值是快照，不提供原子读改写；所有指针仅在回调期间有效。
 */
typedef struct {
    uint16_t device_id;
    uint16_t signal_key;
    size_t signal_index;
    dp_callback_events_t events;
    value_t *value;           /**< GET/SET/CHANGE 的可修改候选值。 */
    const value_t *old_value; /**< SET/CHANGE 的只读 RAM 快照；GET 为 NULL。 */
} dp_callback_context_t;

/** @brief 任一事件返回负值拒绝访问，CHANGE 可修改候选值；指针不得保存。 */
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
    dp_callback_t access_callback; /**< 读、写和变化共用入口，触发由调用方指定。 */
    dp_point_limits_t data;
} dp_point_config_t;

typedef struct {
    const dp_point_config_t *points;
    size_t point_count;
} dp_table_t;

/** @brief 初始化存储及周期保存线程；重复成功调用返回 0。 */
int dp_init(void);

/**
 * @brief 启动线程逐张登记设备点表；描述可临时，行数组及其引用数据必须常驻只读，不得修改或释放。
 * @note 同一张表可登记给多个设备；设备 ID 必须非零且唯一，运行数据由 DP 独立分配。
 * @note 持久点加载历史值；缺失、不兼容或越界时使用初值并同步追加，失败不发布设备。
 * @note 不兼容或越界会警告；真正的存储读取错误直接返回，不覆盖历史记录。
 * @note 登记须串行；启动时执行，或暂停并等待全部 DP 业务访问结束后执行。
 * @retval -ENOMEM 私有登记节点和点数据分配失败。
 */
int dp_register_device(uint16_t device_id, const dp_table_t *table);

/**
 * @brief 按下标读取，按 value_type 返回；GET 回调仅在 READ_WRITEBACK 时更新 RAM。
 * @note READ_WRITEBACK 要求 GET_CALLBACK；未知标志无效；读取忽略 SET/CHANGE 标志。
 * @note 失败时输出不变；回调使用点表内部类型，CHANGE 在提交前处理候选值。
 */
int dp_get_value_by_index(const dp_index_access_t *access, value_t *value);

/**
 * @brief 按下标写入，回调后校验并更新 RAM，成功后返回按 value_type 表示的最终值。
 * @note 忽略读相关标志；失败时输入不变。成功不代表已写入 Flash。
 * @note 调用方须初始化整个 value_t；同值不更新 RAM、时间或 dirty。
 */
int dp_set_value_by_index(const dp_index_access_t *access, value_t *value);

/** @brief 按 key 读取；数值、策略及失败语义与 dp_get_value_by_index 相同。 */
int dp_get_value_by_key(const dp_key_access_t *access, value_t *value);

/** @brief 按 key 写入；数值、策略及失败语义与 dp_set_value_by_index 相同。 */
int dp_set_value_by_key(const dp_key_access_t *access, value_t *value);

/**
 * @brief 将指定设备的点值恢复为配置初值，跳过 SKIP_DEFAULTS 点，不调用业务回调。
 * @note 更新 RAM 并标记持久点待保存；被跳过的点保留值及时间。
 */
int dp_restore_defaults(uint16_t device_id);

/** @brief 出厂恢复成功后、重启前的同步收尾回调；不持有 DP 锁。 */
typedef void (*dp_factory_reset_callback_t)(void *context);

/**
 * @brief 删除指定设备全部持久参数，调用可选回调，然后冷重启；成功不返回。
 * @note 仅线程可用；允许并发 RAM 读写，但不得并发登记或删除设备。
 * @note 不要求设备已登记；没有持久参数也执行回调并重启。
 * @note 保留 RAM 点表至重启；回调不得登记或删除设备。
 * @note 开始删除后暂停该设备 Flash 保存，RAM 读写及默认恢复仍可用；失败可重试。
 * @note 删除失败返回错误，不调用回调、不重启；断电后继续完成已提交的删除。
 * @param device_id 目标设备；DP_DEVICE_ID_ALL 表示所有设备，包括当前未登记的历史设备。
 * @param before_reboot 可为 NULL；回调须返回，不能保证异步日志已发送。
 */
int dp_factory_reset(uint16_t device_id, dp_factory_reset_callback_t before_reboot, void *context);

/**
 * @brief 同步保存待保存参数，阻塞至本轮完成；失败保留待保存标记。
 * @note 仅线程可用。保证完整落盘前，调用方须停止参数修改。
 */
int dp_flush(void);

/**
 * @brief 移除设备、释放私有 RAM 并永久删除其持久参数；不存在返回 0。
 * @note 调用方须停止并等待全部 DP 业务访问结束；内部与后台保存串行。
 * @note 删除失败保留 RAM 并暂停该设备保存，重试或重启继续清理；应用点表不释放。
 */
int dp_remove_device(uint16_t device_id);

#endif
