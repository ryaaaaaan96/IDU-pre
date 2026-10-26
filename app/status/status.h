/**
 * @file
 * @brief 整机状态、故障策略及显式登记的任务看门狗接口。
 *
 * 状态由模块内部单例持有，每次上电只初始化一次，故障复位前不可清除。
 * 业务线程推进阶段，LED 指示独立调度，不表示采样进度。
 */
#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

/** @brief 运行指示灯的标称翻转间隔，单位为毫秒。 */
#define STATUS_LED_HALF_PERIOD_MS 100

/** @brief 整机启动、采样和终止故障阶段。 */
typedef enum {
    STATUS_UNCONFIGURED, /**< 尚未初始化。 */
    STATUS_STARTING, /**< 正在初始化本地服务。 */
    STATUS_RUN, /**< 初始化完成，可以开始采样周期。 */
    STATUS_SAMPLING, /**< 正在采样。 */
    STATUS_PUBLISHING, /**< 正在发布样本。 */
    STATUS_SERVICING_COMMUNICATION, /**< 正在维护通信状态。 */
    STATUS_CHECKING_HEALTH, /**< 正在检查健康状态和镜像确认条件。 */
    STATUS_FAULT_LOCKED, /**< 已锁存终止故障，业务不可恢复。 */
} status_phase_t;

/** @brief 故障发生于初始化或运行阶段，与细分业务阶段独立。 */
typedef enum {
    STATUS_INIT,
    STATUS_RUNTIME,
} status_fault_phase_t;
/** @brief 故障动作按处理强度递增，用于判断是否需要升级。 */
typedef enum {
    STATUS_ACTION_CONTINUE = 0,
    STATUS_ACTION_HOLD = 1,
    STATUS_ACTION_RESET = 2,
} status_action_t;

/** @brief 整机首错及终止动作的一致快照。 */
struct status_application_fault {
    const char *description; /**< 首个故障的常驻描述字符串。 */
    const char *terminal_description; /**< 当前终止故障的常驻描述，无终止故障时为空。 */
    status_fault_phase_t phase; /**< 首个故障发生于初始化或运行阶段。 */
    status_action_t action; /**< 当前锁存的终止动作，显式复位请求可将留驻升级。 */
    int error; /**< 首个故障的错误码。 */
    int64_t occurred_ms; /**< 首个故障发生时的系统运行时间，单位为毫秒。 */
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
 * @note 仅由启动线程调用；APP_DEBUG_SKIP_WATCHDOG 禁用看门狗。
 * @note 所有启动服务成功后，调用方须设置 STATUS_RUN。
 * @note 指示灯初始化失败必须终止启动，运行期 LED 继续策略对此不生效。
 */
int status_init(void);

/**
 * @brief 记录业务阶段，首次离开启动阶段时启动 LED 周期指示。
 * @param[in] phase STATUS_RUN 至 STATUS_CHECKING_HEALTH 之间的阶段。
 * @note 未初始化、终止故障已锁存或目标阶段无效时，不修改状态。
 */
void status_set_phase(status_phase_t phase);

/**
 * @brief 检查喂狗错误，并推进试运行镜像的健康确认。
 * @retval 0 检查通过，或故障策略允许继续。
 * @retval -EACCES 当前不处于 STATUS_CHECKING_HEALTH。
 * @return 其他错误码表示已报告故障；复位策略可能不返回。
 */
int status_progress(void);

/** @brief 读取当前阶段，可在线程或 ISR 中调用。 */
status_phase_t status_phase_get(void);

/**
 * @brief 记录首错并执行调用方指定的最终动作。
 * @param[in] phase 初始化或运行阶段。
 * @param[in] error 非零错误码，通常为负 errno。
 * @param[in] description 非空的常驻描述字符串；记录时保留指针，不复制或格式化。
 * @param[in] action 记录继续、停止业务留驻或请求复位。
 * @retval 0 仅记录后继续。
 * @retval -EINVAL 参数无效，或初始化阶段选择继续。
 * @retval -EACCES 状态尚未初始化。
 * @return 终止动作在复位未接管时返回传入的错误码。
 * @note 可在 ISR 中调用；不按描述字符串选择策略，不隐式转换调试动作。
 * @note 首错不覆盖；终止后不可恢复，显式 RESET 可将 HOLD 升级为复位。
 */
int status_report_fault(status_fault_phase_t phase, int error,
                        const char *description, status_action_t action);

/** @brief 任务处于活动期限监控状态。 */
#define STATUS_TASK_ACTIVE UINT32_C(1)
/** @brief 任务已显式退出期限监控。 */
#define STATUS_TASK_SLEEP_FOREVER UINT32_C(2)
/** @brief 任务已触发超时故障。 */
#define STATUS_TASK_FAULT UINT32_C(4)

/** @brief 任务登记配置，字段值在登记时复制。 */
struct status_task_config {
    const char *name; /**< 非空名称字符串，登记后须持续有效。 */
    uint32_t timeout_ms; /**< 最大心跳间隔，单位为毫秒，必须大于零。 */
};

/** @brief 任务监控状态的只读快照。 */
struct status_task_snapshot {
    const char *name; /**< 调用方持有的任务名称。 */
    uint32_t timeout_ms; /**< 最大心跳间隔，单位为毫秒。 */
    int64_t last_feed_ms; /**< 最近登记或心跳时的系统运行时间，单位为毫秒。 */
    uint32_t flags; /**< 0 表示未登记，非零值为 STATUS_TASK_* 状态标志。 */
};

/**
 * @brief 调用方持有的常驻任务上下文。
 * @note 首次使用前清零；登记后通过快照接口查询，不得修改内部字段。
 */
struct status_task {
    struct status_task_snapshot state; /**< 内部维护的诊断状态。 */
    int64_t deadline_ticks; /**< 内部维护的绝对到期时刻，单位为系统时钟节拍。 */
    struct status_task *next; /**< 内部活动任务链表链接。 */
};

/** @brief 首个任务超时和当前外部喂狗错误的快照。 */
struct status_fault_snapshot {
    struct status_task_snapshot task; /**< 超时任务的诊断状态。 */
    int64_t occurred_ms; /**< 超时发生时的系统运行时间，单位为毫秒。 */
    int error; /**< 任务超时错误码。 */
    int feed_error; /**< 查询时的底层看门狗错误码。 */
};

/**
 * @brief 登记任务并立即开始期限监控，也可重新启用已退出监控的任务。
 * @param[in,out] task 首次使用前已清零的常驻任务上下文。
 * @param[in] config 任务名称和超时配置；名称须持续有效。
 * @retval 0 登记成功。
 * @retval -EINVAL 参数、名称或超时间隔无效。
 * @retval -EALREADY 任务已处于活动监控状态。
 * @retval -ECANCELED 已锁存终止故障或任务超时。
 * @return 其他负错误码表示服务尚不可用或底层喂狗失败。
 * @note 仅在线程中调用；失败保持上下文不变，不分配内存或占用固定槽位。
 */
int status_register(struct status_task *task, const struct status_task_config *config);

/**
 * @brief 报告活动任务进展并刷新期限。
 * @param[in,out] task 已登记且处于活动状态的任务上下文。
 * @retval 0 心跳已接收。
 * @retval -EACCES 任务未启用监控或服务不可用。
 * @retval -ETIMEDOUT 心跳到达时已经超时，并触发故障策略。
 * @retval -ECANCELED 已锁存终止故障或任务超时。
 * @return 其他负错误码表示参数、调用上下文或底层喂狗错误。
 * @note 仅在线程中调用；不能通过迟到心跳撤销故障。
 */
int status_feed(struct status_task *task);

/**
 * @brief 取消任务的期限监控，保留休眠标志和诊断值。
 * @param[in,out] task 已登记的任务上下文。
 * @retval 0 已退出监控，或此前已经退出。
 * @retval -ETIMEDOUT 退出前已超时，并触发故障策略。
 * @retval -ECANCELED 已锁存终止故障或任务超时。
 * @return 其他负错误码表示参数、服务状态或底层喂狗错误。
 * @note 仅在线程中调用，不使业务线程实际休眠；须重新登记才能恢复监控。
 */
int status_sleep_forever(struct status_task *task);

/**
 * @brief 读取任务快照，不修改状态，可在线程或 ISR 中调用。
 * @param[in] task 待查询的任务上下文。
 * @param[out] snapshot 接收快照的存储；失败时保持原值。
 * @retval 0 查询成功。
 * @retval -EINVAL 参数为空。
 * @retval -ENOENT 此任务从未登记。
 */
int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot);

/**
 * @brief 读取首个任务超时快照，不修改状态，可在线程或 ISR 中调用。
 * @param[out] snapshot 接收快照的存储；失败时保持原值。
 * @retval 0 查询成功。
 * @retval -EINVAL 输出指针为空。
 * @retval -ENOENT 尚未发生任务超时。
 */
int status_fault_get(struct status_fault_snapshot *snapshot);


#endif
