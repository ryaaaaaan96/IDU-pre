#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* 指示节奏归 status 管理，不表示采样进度。 */
#define STATUS_LED_HALF_PERIOD_MS 100

enum status_phase {
    STATUS_UNCONFIGURED,
    STATUS_STARTING,
    STATUS_RUN, /* 初始化完成，可以开始采样周期。 */
    STATUS_SAMPLING,
    STATUS_PUBLISHING,
    STATUS_SERVICING_COMMUNICATION,
    STATUS_CHECKING_HEALTH,
    STATUS_FAULT_LOCKED,
};

enum status_fault {
    STATUS_FAULT_NONE, STATUS_FAULT_INPUT_INIT, STATUS_FAULT_IAP_INIT,
    STATUS_FAULT_PROTOCOL_INIT, STATUS_FAULT_SAMPLE, STATUS_FAULT_PUBLISH,
    STATUS_FAULT_PROTOCOL, STATUS_FAULT_LED, STATUS_FAULT_WATCHDOG,
    STATUS_FAULT_IMAGE_CONFIRM, STATUS_FAULT_TASK_TIMEOUT, STATUS_FAULT_COUNT,
};
enum status_action { STATUS_CONTINUE, STATUS_HOLD, STATUS_RESET };
struct status_application_fault {
    enum status_fault first, terminal;
    enum status_phase phase;
    enum status_action action;
    int error;
    int64_t occurred_ms;
};
/* 一致的 RAM 诊断快照，也覆盖 ISR 报告的故障。 */
int status_application_fault_get(struct status_application_fault *snapshot);

struct status_degradation {
    uint16_t remote_offline, remote_initializing;
    int storage_error;
};
/* 协议诊断不会清除终止故障，也不会停止本地采样。 */
void status_update_degradation(uint16_t offline, uint16_t initializing, int storage_error);
struct status_degradation status_degradation_get(void);

/* 产品状态为私有单例，每次上电仅初始化一次；
 * 重复初始化返回 -EALREADY。采样阶段切换仅记录进度，
 * LED 策略归 status.c。故障保留首次阶段和错误，
 * 复位前不可清除。
 */
/* 初始化状态、LED 和看门狗（启用 APP_DEBUG_SKIP_WATCHDOG 时跳过看门狗）。
 * 所有启动服务成功后，应用进入 STATUS_RUN。 */
int status_init(void);
void status_set_phase(enum status_phase phase);
int status_progress(void);
enum status_phase status_phase_get(void);
/* 可在 ISR 中调用，并保留首个错误。继续策略返回 0；
 * 终止策略在复位未接管时返回错误。喂狗失败可将留驻故障升级为复位。 */
int status_report_fault(enum status_fault fault,
                        enum status_phase phase, int error);

#define STATUS_TASK_ACTIVE UINT32_C(1)
#define STATUS_TASK_SLEEP_FOREVER UINT32_C(2)
#define STATUS_TASK_FAULT UINT32_C(4)

struct status_task_config {
    const char *name;
    uint32_t timeout_ms;
};
struct status_task_snapshot {
    const char *name;
    uint32_t timeout_ms;
    int64_t last_feed_ms;
    uint32_t flags;
};
/* 对象由调用方持有，仅清零一次并保持存活。字段属于内部状态，
 * 应通过快照接口访问，不得直接修改。 */
struct status_task {
    struct status_task_snapshot state;
    int64_t deadline_ticks;
    struct status_task *next;
    bool registered;
};
struct status_fault_snapshot {
    struct status_task_snapshot task;
    int64_t occurred_ms;
    int error;
    int feed_error;
};
/* 配置值会复制，任务对象和名称仍归调用方持有。
 * 登记、心跳和休眠仅在线程中调用；快照查询也可在 ISR 中调用。 */
/* 登记后立即开始计时；重复登记活动任务返回 -EALREADY。
 * 没有固定槽位或动态分配，每个调用方提供自己的上下文。失败不修改上下文；
 * 全局故障锁存后，登记、心跳和休眠返回 -ECANCELED，直到复位。 */
int status_register(struct status_task *task, const struct status_task_config *config);
int status_feed(struct status_task *task);
/* 仅取消此任务的期限，保留 STATUS_TASK_SLEEP_FOREVER 和诊断值；
 * 操作幂等。status_feed 不能恢复监控，须重新登记。 */
int status_sleep_forever(struct status_task *task);
/* 从未登记或没有任务故障时返回 -ENOENT；查询不修改状态。 */
int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot);
int status_fault_get(struct status_fault_snapshot *snapshot);


#endif
