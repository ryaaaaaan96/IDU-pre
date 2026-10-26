/**
 * @file
 * @brief 测点处理事务和业务调度接口。
 */
#ifndef DP_PROCESS_H
#define DP_PROCESS_H
#include <dataprocess/points.h>

/**
 * @brief 这些是操作标志，与持久化测点属性相互独立。
 */
#define DP_OP_NO_COLLECT BIT(0)
#define DP_OP_NO_DM      BIT(1)
#define DP_OP_NO_STORE   BIT(2)
#define DP_OP_DEFER      BIT(3)
#define DP_OP_MASK       0x0fU

typedef enum { DP_OP_WRITE, DP_OP_READ, DP_OP_INIT, DP_OP_TIMER, DP_OP_SAMPLE } dp_operation_t;
struct dp_process_item {
    struct dp_point_update point;
    uint8_t flags;
    /**
     * @brief 原始快速设置条目可携带不同的寄存器地址。
     */
    bool reference_address_valid;
    uint16_t reference_address;
};
struct dp_process_request {
    uint32_t id;
    dp_operation_t operation;
    uint16_t port_id;
    uint16_t quality_flags;
    int64_t sampled_at;
    uint16_t address;
    uint16_t register_count;
    uint8_t function;
    uint8_t count;
    /**
     * @brief 全局 INIT/TIMER 回调，仅在输入列表为空时有效。
     */
    int (*callback)(struct dp_process_ctx *ctx, void *user);
    void *user;
    DM_PF_SIG_PROC reference_callback;
    struct dp_process_item items[CONFIG_DP_PROCESS_MAX_OUTPUTS];
};
struct dp_process_result {
    uint32_t id;
    int error;
    uint8_t count;
    struct { struct dp_key key; int error; } points[CONFIG_DP_PROCESS_MAX_OUTPUTS];
};
struct dp_process_ctx {
    const struct dp_process_request *request;
    void *transaction;
    uint8_t finish_actions;
    int (*action)(uint16_t argument);
    uint16_t action_argument;
};
typedef void (*dp_process_done_fn)(const struct dp_process_result *result, void *user);

/**
 * @brief CONFIG_DP_PROCESS 独立于 Flash/NVS 存储提供这些接口。
 *
 * 同步接口仅在线程中调用，回调只使用下方事务访问器。先校验再产生副作用； 各设备分组分别提交，结果报告部分完成情况。 已接收的异步请求恰好得到一次完成通知。
 */
int dp_process_execute(const struct dp_process_request *request,
                        struct dp_process_result *result);
/**
 * @brief 非阻塞地复制到有界队列，并发发布者可能收到 -EBUSY。
 *
 * 通用回调的用户数据须存活到排队执行完成。
 */
int dp_process_submit(const struct dp_process_request *request,
                       dp_process_done_fn done, void *user);
/**
 * @brief 由应用创建的业务任务，与样本消费者独立。
 */
void dp_business_task(void *unused1, void *unused2, void *unused3);
int dp_txn_get(struct dp_process_ctx *ctx, struct dp_key key,
                bool old, struct dp_value *value);
bool dp_txn_contains(struct dp_process_ctx *ctx, struct dp_key key);
int dp_txn_emit(struct dp_process_ctx *ctx, struct dp_key key,
                 const struct dp_value *value, uint8_t flags);
/**
 * @brief 标志仅作用于已捕获的输出，不创建新写入。
 */
int dp_txn_get_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t *flags);
int dp_txn_set_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t flags);
int dp_txn_register_old(struct dp_process_ctx *ctx, struct dp_key key, const struct dp_value *value);
int dp_reference_read_at(struct dp_key key, uint16_t port_id, uint8_t function,
                         uint16_t address, uint16_t count, struct dp_point_state *state);
int dp_txn_get_state(struct dp_process_ctx *ctx, struct dp_key key,
                       struct dp_point_state *state);
/**
 * @brief 由唯一的 zbus 样本消费者在纯转换后调用。
 */
int dp_process_sample(const struct dp_point_batch *batch);
int dp_process_init_device(uint16_t dev_id);
int dp_process_read_at(struct dp_key key, uint16_t port_id, uint8_t function,
                        uint16_t address, uint16_t count, struct dp_point_state *state);
int dp_process_read(struct dp_key key, uint16_t port_id, struct dp_point_state *state);
/**
 * @brief 参考 INIT/TIMER 登记表的声明式对应形式。
 *
 * 键绑定普通测点处理函数，保留事务上下文。 延时使用有限的 Zephyr 相对超时， K_NO_WAIT 周期表示仅执行一次。
 */
struct dp_timer_spec {
    struct dp_key key;
    k_timeout_t delay;
    k_timeout_t period;
};
/**
 * @brief 启动计划中未登记的初始化键/定时任务，不预留执行项。
 */
#define DP_PROCESS_DEFAULT_INIT_KEYS NULL
#define DP_PROCESS_DEFAULT_TIMERS NULL
#define DP_PROCESS_DEFAULT_COUNT 0U

struct dp_process_plan {
    bool initialize_all;
    const struct dp_key *init_keys;
    size_t init_count;
    const struct dp_timer_spec *timers;
    size_t timer_count;
};
/**
 * @brief 在线程启动阶段、dp_init_config 和可选参数恢复后调用。
 *
 * 按顺序执行 INIT 前先校验整个计划并预留定时器槽位。 所有 INIT 成功后才启动计划定时器；失败释放预留，可重试。 此前成功的
 * INIT 事务不回滚。 initialize_all 与显式 init_keys 互斥； 成功初始化仅允许一次，重复返回
 * -EALREADY。 计划只在本次调用中使用。
 */
int dp_process_init(const struct dp_process_plan *plan);
/**
 * @brief 可取消活动槽位；一次性任务完成后释放槽位。
 */
int dp_process_timer_start(struct dp_key key, uint32_t delay_ms, uint32_t period_ms);
int dp_process_timer_cancel(int slot);
/**
 * @brief 要求 CONFIG_DP_PARAMS，否则返回 -ENOTSUP。
 */
int dp_process_save_reboot(uint32_t delay_ms);
/**
 * @brief 仅用于 CONFIG_DP_PARAMS。
 *
 * 板级可覆盖以实现受控关闭， 仅在刷新成功后调用。
 */
void dp_process_reboot(void);

/**
 * @brief 引擎内部接口，也可供后端适配实现使用；不调用业务回调。
 */
int dp_check_value(const struct dp_point_cfg *p, const struct dp_value *v);
int dp_commit_sample(const struct dp_point_update *values, size_t count,
                              uint16_t flags, int64_t sampled_at);
int dp_commit(const struct dp_point_update *values, size_t count);
#endif
