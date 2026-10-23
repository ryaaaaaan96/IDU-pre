#ifndef DP_PROCESS_H
#define DP_PROCESS_H
#include <dataprocess/points.h>

/* These are operation flags, independent of persistent point attributes. */
#define DP_OP_NO_COLLECT BIT(0)
#define DP_OP_NO_DM      BIT(1)
#define DP_OP_NO_STORE   BIT(2)
#define DP_OP_DEFER      BIT(3)
#define DP_OP_MASK       0x0fU

enum dp_operation { DP_OP_WRITE, DP_OP_READ, DP_OP_INIT, DP_OP_TIMER, DP_OP_SAMPLE };
struct dp_process_item {
    struct dp_point_update point;
    uint8_t flags;
    /* Original fast-set entries can carry different register addresses. */
    bool reference_address_valid;
    uint16_t reference_address;
};
struct dp_process_request {
    uint32_t id;
    enum dp_operation operation;
    uint16_t port_id;
    uint16_t quality_flags;
    int64_t sampled_at;
    uint16_t address;
    uint16_t register_count;
    uint8_t function;
    uint8_t count;
    /* Global INIT/TIMER callback; valid only with an empty input list. */
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

/* CONFIG_DP_PROCESS supplies these APIs independently of flash/NVS storage.
 * Synchronous thread API. Callbacks use only the txn accessors below.
 * Validation precedes effects. Device groups commit separately: result reports
 * partial completion; an accepted async request receives exactly one completion.
 */
int dp_process_execute(const struct dp_process_request *request,
                        struct dp_process_result *result);
/* Nonblocking copy into the bounded queue; simultaneous publishers may receive
 * -EBUSY. Generic callback user data must outlive queued execution.
 */
int dp_process_submit(const struct dp_process_request *request,
                       dp_process_done_fn done, void *user);
/* Application-created business worker, independent of the sample consumer. */
void dp_business_task(void *unused1, void *unused2, void *unused3);
int dp_txn_get(struct dp_process_ctx *ctx, struct dp_key key,
                bool old, struct dp_value *value);
bool dp_txn_contains(struct dp_process_ctx *ctx, struct dp_key key);
int dp_txn_emit(struct dp_process_ctx *ctx, struct dp_key key,
                 const struct dp_value *value, uint8_t flags);
/* Flags address already captured outputs; they do not create a write. */
int dp_txn_get_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t *flags);
int dp_txn_set_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t flags);
int dp_txn_register_old(struct dp_process_ctx *ctx, struct dp_key key, const struct dp_value *value);
int dp_reference_read_at(struct dp_key key, uint16_t port_id, uint8_t function,
                         uint16_t address, uint16_t count, struct dp_point_state *state);
int dp_txn_get_state(struct dp_process_ctx *ctx, struct dp_key key,
                       struct dp_point_state *state);
/* Called by the single zbus sample consumer after pure transforms. */
int dp_process_sample(const struct dp_point_batch *batch);
int dp_process_init_device(uint16_t dev_id);
int dp_process_read_at(struct dp_key key, uint16_t port_id, uint8_t function,
                        uint16_t address, uint16_t count, struct dp_point_state *state);
int dp_process_read(struct dp_key key, uint16_t port_id, struct dp_point_state *state);
/* Declarative counterparts of the reference INIT/TIMER registration tables.
 * Keys bind to normal point handlers, retaining the transaction context.
 * Delays are finite, relative Zephyr timeouts; K_NO_WAIT period is one-shot.
 */
struct dp_timer_spec {
    struct dp_key key;
    k_timeout_t delay;
    k_timeout_t period;
};
/* 启动计划中未登记的初始化键/定时任务，不预留执行项。 */
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
/* Thread startup API, after dp_init_config and optional parameter recovery. Validates
 * the whole plan and reserves timer slots before ordered INIT calls. Plan timers
 * start only if every INIT succeeds; failure releases reservations for retry.
 * Earlier successful INIT transactions are not rolled back. initialize_all and
 * explicit init_keys are mutually exclusive. Successful initialization is one-time;
 * repeated calls return -EALREADY. The plan is consumed during this call.
 */
int dp_process_init(const struct dp_process_plan *plan);
/* Active slot can be cancelled; a completed one-shot frees its slot. */
int dp_process_timer_start(struct dp_key key, uint32_t delay_ms, uint32_t period_ms);
int dp_process_timer_cancel(int slot);
/* Requires CONFIG_DP_PARAMS; otherwise returns -ENOTSUP. */
int dp_process_save_reboot(uint32_t delay_ms);
/* CONFIG_DP_PARAMS only. Board may override for controlled shutdown; called
 * only after successful flush.
 */
void dp_process_reboot(void);

/* Engine internals also usable by backend adapters; no business callbacks. */
int dp_check_value(const struct dp_point_cfg *p, const struct dp_value *v);
int dp_commit_sample(const struct dp_point_update *values, size_t count,
                              uint16_t flags, int64_t sampled_at);
int dp_commit(const struct dp_point_update *values, size_t count);
#endif
