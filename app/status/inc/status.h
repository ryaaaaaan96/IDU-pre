/**
 * @file
 * @brief 整机状态与故障策略接口。
 *
 * 状态由模块内部单例持有，每次上电只初始化一次，故障复位前不可清除。
 * 业务线程设置运行状态，LED 指示独立调度，不表示采样进度。
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

/* 整机状态与故障接口。 */

/** @brief 整机初始化、运行和终止故障状态。 */
typedef enum {
    STATUS_UNCONFIGURED, /**< 尚未初始化。 */
    STATUS_STARTING, /**< 正在初始化本地服务。 */
    STATUS_RUN, /**< 初始化完成，可以开始采样周期。 */
    STATUS_FAULT_LOCKED, /**< 已锁存终止故障，业务不可恢复。 */
} status_phase_t;

/** @brief 故障发生于初始化或运行阶段，与细分业务阶段独立。 */
typedef enum {
    STATUS_INIT,
    STATUS_RUNTIME,
} status_fault_phase_t;
/** @brief 调用方指定的故障处理动作。 */
typedef enum {
    STATUS_ACTION_CONTINUE = 0,
    STATUS_ACTION_HOLD = 1,
    STATUS_ACTION_RESET = 2,
} status_action_t;

/** @brief 整机最近一次故障的一致快照。 */
struct status_application_fault {
    const char *description; /**< 最近一次故障的常驻描述字符串。 */
    status_fault_phase_t phase; /**< 最近一次故障发生于初始化或运行阶段。 */
    status_action_t action; /**< 最近一次故障请求的处理动作，不代表业务是否已停止。 */
    int error; /**< 最近一次故障的错误码。 */
    int64_t occurred_ms; /**< 最近一次故障发生时的系统运行时间，单位为毫秒。 */
};

/**
 * @brief 读取整机故障快照，可在线程或 ISR 中调用。
 * @param[out] snapshot 接收快照的存储；失败时保持原值。
 * @retval 0 已复制故障快照。
 * @retval -EINVAL 输出指针为空。
 * @retval -ENOENT 尚未记录故障。
 */
int status_application_fault_get(struct status_application_fault *snapshot);

/** @brief 远端和存储的降级诊断，与本地终止故障独立。 */
struct status_degradation {
    uint16_t remote_offline, remote_initializing; /**< 离线和正在初始化的远端设备数量。 */
    int storage_error; /**< 当前存储故障原因，恢复就绪后清零。 */
};

/**
 * @brief 原子更新降级诊断，不清除终止故障或停止本地采样。
 * @param[in] offline 离线远端设备数。
 * @param[in] initializing 正在初始化的远端设备数。
 * @param[in] storage_error 存储故障原因，无故障时为 0。
 */
void status_update_degradation(uint16_t offline, uint16_t initializing, int storage_error);

/** @brief 读取一致的降级诊断副本，可在线程或 ISR 中调用。 */
struct status_degradation status_degradation_get(void);

/**
 * @brief 初始化状态单例、LED 和看门狗。
 * @retval 0 指示灯及所需看门狗初始化成功。
 * @retval -EALREADY 本次上电已调用过初始化。
 * @return 其他错误码表示初始化故障；复位策略可能不返回。
 * @note 仅由启动线程调用；软件监控与外部喂狗分别由 WATCHDOG_SOFTWARE / WATCHDOG_EXTERNAL 控制。
 * @note 所有启动服务成功后，调用方须设置 STATUS_RUN。
 * @note 指示灯初始化失败必须终止启动，运行期 LED 继续策略对此不生效。
 */
int status_init(void);

/**
 * @brief 记录业务阶段，首次离开启动阶段时启动 LED 周期指示。
 * @param[in] phase STATUS_RUN 运行状态。
 * @note 未初始化、终止故障已锁存或目标阶段无效时，不修改状态。
 */
void status_set_phase(status_phase_t phase);

/**
 * @brief 检查喂狗错误，并推进试运行镜像的健康确认。
 * @retval 0 检查通过，或故障策略允许继续。
 * @retval -EACCES 当前不处于 STATUS_RUN。
 * @return 其他错误码表示已报告故障；复位策略可能不返回。
 */
int status_progress(void);

/** @brief 读取当前阶段，可在线程或 ISR 中调用。 */
status_phase_t status_phase_get(void);

/**
 * @brief 记录本次故障并执行调用方指定的动作。
 * @param[in] phase 初始化或运行阶段。
 * @param[in] error 非零错误码，通常为负 errno。
 * @param[in] description 非空的常驻描述字符串；记录时保留指针，不复制或格式化。
 * @param[in] action 记录继续、停止业务留驻或请求复位。
 * @retval 0 仅记录后继续。
 * @retval -EINVAL 参数无效，或初始化阶段选择继续。
 * @retval -EACCES 状态尚未初始化。
 * @return 终止动作在复位未接管时返回传入的错误码。
 * @note 可在 ISR 中调用；开启日志时要求 LOG_MODE_DEFERRED、关闭 LOG_BLOCK_IN_THREAD，不等待日志后端。
 * @note 故障门控和 RESET 停喂/复位同步执行，不等待工作队列；LED 更新延后。
 * @note 不按描述字符串选择策略，不隐式转换调试动作。
 * @note 每次更新故障快照；已停止业务不会因后续 CONTINUE 恢复。
 */
int status_report_fault(status_fault_phase_t phase, int error,
                        const char *description, status_action_t action);

#endif
