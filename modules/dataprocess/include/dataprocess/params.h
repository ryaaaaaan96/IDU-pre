/**
 * @file
 * @brief 参数事务、持久化和南向设定接口。
 */
#ifndef DP_PARAMS_H
#define DP_PARAMS_H

#include <dataprocess/points.h>

#define DP_PARAM_WRITE   BIT(0)
#define DP_PARAM_PERSIST BIT(1)
#define DP_PARAM_RESTORE BIT(2)
#define DP_PARAM_FORWARD BIT(3)
#define DP_PARAM_DEFER_STORE BIT(4)
#define DP_PARAM_STORAGE_READY BIT(0)
#define DP_PARAM_PENDING BIT(1)
#define DP_PARAM_HISTORY_RECOVERED BIT(2)
#define DP_PARAM_VALUE_CONVERTED BIT(3)
#define DP_PARAM_STORAGE_RESET BIT(4)

struct dp_param_status {
    uint32_t sequence;
    int error; /**< 最近一次操作结果，与存储健康状态独立。 */
    uint16_t flags;
    /**
     * @brief 存储不可用原因；仅在存储重新就绪后清除。
     */
    int storage_error;
};

/**
 * @brief 由应用提供南向命令和绝对设定值的访问实现。
 *
 * 非持久命令只写入， 持久设定要求读回。回调在参数事务锁内同步执行，可阻塞等待传输， 但不得重入参数接口；成功返回 0，否则返回传输错误。 读取回调不得重放写入。
 */
struct dp_params_transport {
    int (*read)(void *user, uint16_t dev_id, uint16_t address,
                uint16_t *words, size_t count);
    int (*write)(void *user, uint16_t dev_id, uint16_t address,
                 const uint16_t *words, size_t count);
    bool (*simulated)(void *user, uint16_t dev_id);
    void *user;
};

/**
 * @brief 初始化前复制回调，NULL 表示清除；相同登记具有幂等性。
 *
 * 初始化后替换后端返回 -EBUSY。缺少后端时，本地参数仍可用； 转发操作在写事务意图或修改缓存前返回 -ENODEV。
 */
int dp_params_set_transport(const struct dp_params_transport *transport);

/**
 * @brief 仅挂载指定分区，按 (dev_id, sig_id) 持久化数值； 默认值和范围仍在处理表中。
 *
 * 记录格式不兼容时仅清除此分区一次， 输出警告并设置 STORAGE_RESET，不迁移旧快照。 I/O 错误会禁用持久写入，不触发擦除。
 * 批次日志恢复完整值集， 南向事务意图仅通过读回核对。初始化失败仍保留可读默认值。 存储值表示期望配置，南向实时遥测单独维护。
 */
int dp_params_init(const struct dp_equip_cfg *cfg, uint8_t area_id);
/**
 * @brief 采集反馈只修改实时缓存，待完成写入优先。
 */
int dp_params_sample(const struct dp_point_update *values, size_t count,
                     uint16_t flags, int64_t sampled_at);
int dp_params_validate(const struct dp_equip_cfg *cfg);
int dp_params_read(struct dp_key key, struct dp_point_state *state);
/**
 * @brief 业务侧按键写入，使用与 Modbus 相同的校验和持久化规则。
 *
 * error 必须为 0；远端多点请求必须属于一个连续单元。
 */
int dp_params_set_values(const struct dp_point_update *values, size_t count);
int dp_params_apply(const struct dp_point_update *values, size_t count, uint8_t flags);
/**
 * @brief 业务任务在去抖期限到期后刷新延迟写入的值。
 */
int dp_params_flush_due(void);
int dp_params_sync_device(uint16_t dev_id);
/**
 * @brief 旧地址写入接口：调用方必须显式选择不可变映射视图。
 *
 * 产品协议使用 TR， 业务代码优先按键写入。
 */
int dp_params_write_registers(const struct dp_equip_cfg *mapping, uint16_t address,
                               const uint16_t *words, size_t count);
int dp_params_restore(uint16_t dev_id);
int dp_params_flush(void);
/**
 * @brief 原子地刷新存储并拒绝新写入，直到复位或取消。
 */
int dp_params_prepare_reboot(void);
void dp_params_cancel_reboot(void);
/**
 * @brief 仅通过 READBACK 核对持久化的不确定事务，绝不重放写入。
 */
int dp_params_recover(void);
/**
 * @brief 重新挂载并加载持久配置；调用方须先让应用使用者停止访问。
 */
int dp_params_reload(void);
void dp_params_get_status(struct dp_param_status *status);
ZBUS_CHAN_DECLARE(dp_param_events);

#endif
