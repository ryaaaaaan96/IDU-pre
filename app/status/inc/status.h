/**
 * @file
 * @brief 整机状态与故障策略接口。
 *
 * 状态由模块内部单例持有，每次上电只初始化一次，故障复位前不可清除。
 * watchdog 故障通道仅用于本次启动不可恢复的终止故障：HOLD 等待人工重启，RESET 自动重启。
 * 可恢复异常由业务模块处理，不进入整机故障锁存；重启不保证排除实际故障原因。
 * 业务线程设置运行状态，LED 指示独立调度，不表示采样进度。
 */
#ifndef STATUS_H
#define STATUS_H

/* 整机状态与故障接口。 */

/** @brief 整机初始化、运行和终止故障状态。 */
typedef enum {
    STATUS_UNCONFIGURED, /**< 尚未初始化。 */
    STATUS_STARTING,     /**< 正在初始化本地服务。 */
    STATUS_RUN,          /**< 初始化完成，可以开始采样周期。 */
    STATUS_FAULT_LOCKED, /**< 已锁存终止故障，业务不可恢复。 */
    STATUS_PHASE_COUNT,  /**< 阶段数量；用于内部显示表等定长数组。 */
} status_phase_t;

/**
 * @brief 初始化状态单例，绑定唯一故障回调并启动看门狗，然后初始化 LED。
 * @retval 0 指示灯及所需看门狗初始化成功。
 * @retval -EALREADY 本次上电已调用过初始化。
 * @return 其他错误码表示初始化故障；复位策略可能不返回。
 * @note 仅由启动线程调用；软件监控随模块初始化运行，外部喂狗由 WATCHDOG_EXTERNAL 控制。
 * @note main 只调用本接口，不单独初始化看门狗；watchdog 不依赖 status。
 * @note 所有启动服务成功后，调用方须设置 STATUS_RUN。
 * @note 指示灯初始化失败必须终止启动，运行期 LED 继续策略对此不生效。
 */
int status_init(void);

/**
 * @brief 记录业务阶段，并切换到对应的 LED 指示状态。
 * @param[in] phase STATUS_RUN 运行状态。
 * @note 未初始化、终止故障已锁存或目标阶段无效时，不修改状态。
 */
void status_set_phase(status_phase_t phase);

/** @brief 读取当前阶段，可在线程或 ISR 中调用。 */
status_phase_t status_phase_get(void);

#endif
