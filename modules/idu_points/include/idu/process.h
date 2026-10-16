#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H
#include <idu/points.h>

/* These are operation flags, independent of persistent point attributes. */
#define IDU_OP_NO_COLLECT BIT(0)
#define IDU_OP_NO_DM      BIT(1)
#define IDU_OP_NO_STORE   BIT(2)
#define IDU_OP_DEFER      BIT(3)
#define IDU_OP_MASK       0x0fU

enum idu_operation { IDU_OP_WRITE, IDU_OP_READ, IDU_OP_INIT, IDU_OP_TIMER, IDU_OP_SAMPLE };
struct idu_process_item {
    struct idu_point_update point;
    uint8_t flags;
};
struct idu_process_request {
    uint32_t id;
    enum idu_operation operation;
    uint16_t port_id;
    uint16_t quality_flags;
    int64_t sampled_at;
    uint16_t address;
    uint16_t register_count;
    uint8_t function;
    uint8_t count;
    struct idu_process_item items[CONFIG_IDU_PROCESS_MAX_OUTPUTS];
};
struct idu_process_result {
    uint32_t id;
    int error;
    uint8_t count;
    struct { struct idu_key key; int error; } points[CONFIG_IDU_PROCESS_MAX_OUTPUTS];
};
struct idu_process_ctx {
    const struct idu_process_request *request;
    void *transaction;
};
typedef void (*idu_process_done_fn)(const struct idu_process_result *result, void *user);

/* Synchronous thread API. Callbacks use only the txn accessors below.
 * Validation precedes effects. Device groups commit separately: result reports
 * partial completion; an accepted async request receives exactly one completion.
 */
int idu_process_execute(const struct idu_process_request *request,
                        struct idu_process_result *result);
int idu_process_submit(const struct idu_process_request *request,
                       idu_process_done_fn done, void *user);
int idu_txn_get(struct idu_process_ctx *ctx, struct idu_key key,
                bool old, struct idu_value *value);
bool idu_txn_contains(struct idu_process_ctx *ctx, struct idu_key key);
int idu_txn_emit(struct idu_process_ctx *ctx, struct idu_key key,
                 const struct idu_value *value, uint8_t flags);
int idu_txn_get_state(struct idu_process_ctx *ctx, struct idu_key key,
                       struct idu_point_state *state);
/* Called by the single zbus sample consumer after pure transforms. */
int idu_process_sample(const struct idu_point_batch *batch);
int idu_process_init_device(uint16_t dev_id);
int idu_process_read_at(struct idu_key key, uint16_t port_id, uint8_t function,
                        uint16_t address, uint16_t count, struct idu_point_state *state);
int idu_process_read(struct idu_key key, uint16_t port_id, struct idu_point_state *state);
/* Active slot can be cancelled; a completed one-shot frees its slot. */
int idu_process_timer_start(struct idu_key key, uint32_t delay_ms, uint32_t period_ms);
int idu_process_timer_cancel(int slot);
int idu_process_save_reboot(uint32_t delay_ms);
/* Board may override for controlled shutdown; called only after successful flush. */
void idu_process_reboot(void);

/* Engine internals also usable by backend adapters; no business callbacks. */
const struct idu_equip_cfg *idu_points_config(void);
const struct idu_point_cfg *idu_points_find(struct idu_key key);
int idu_points_check_value(const struct idu_point_cfg *p, const struct idu_value *v);
int idu_points_commit_sample(const struct idu_point_update *values, size_t count,
                              uint16_t flags, int64_t sampled_at);
int idu_points_commit(const struct idu_point_update *values, size_t count);
#endif
