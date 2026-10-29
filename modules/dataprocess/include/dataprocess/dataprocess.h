/** @file @brief 点表登记、同步点值读写。 */
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

/** @brief 同步回调上下文；身份只读，value 可修改，所有指针仅在回调期间有效。 */
typedef struct {
    uint16_t device_id;
    uint16_t signal_key;
    bool is_write;       /**< true 为写回调，false 为读回调。 */
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
 * @brief 同步写点，回调后校验并更新 RAM；成功不代表已写入 Flash。
 * @note 按点表类型填写 value 对应成员，调用方须将整个联合体初始化；输入不被修改。
 */
int dp_set_para(uint16_t device_id, uint16_t signal_key, const value_t *value);

/** @brief 同步读点，可执行读回调；成功返回 0，失败时输出保持不变。 */
int dp_get_para(uint16_t device_id, uint16_t signal_key, value_t *value);

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
