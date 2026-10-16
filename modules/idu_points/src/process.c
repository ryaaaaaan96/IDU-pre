#include <idu/process.h>
#include <idu/params.h>
#include <errno.h>
#include <string.h>
#include <zephyr/sys/reboot.h>

struct entry {
    struct idu_process_item item;
    struct idu_point_state old;
    bool dirty;
};
static struct entry entries[CONFIG_IDU_PROCESS_MAX_OUTPUTS];
static size_t entry_count;
static uint16_t transaction_quality;
static k_tid_t owner;
K_MUTEX_DEFINE(process_lock);
static struct idu_point_update group[CONFIG_IDU_PROCESS_MAX_OUTPUTS];
struct async_job {
    struct idu_process_request request;
    idu_process_done_fn done;
    void *user;
};
K_MSGQ_DEFINE(process_jobs, sizeof(struct async_job), CONFIG_IDU_PROCESS_QUEUE_DEPTH, 4);
struct business_timer { struct idu_key key; int64_t due; uint32_t period, revision; bool active; };
static uint32_t timer_revision;
static struct business_timer timers[CONFIG_IDU_PROCESS_TIMERS];
K_MUTEX_DEFINE(timer_lock);
static int64_t reboot_at;

static bool equal(struct idu_key a, struct idu_key b)
{
    return a.dev_id == b.dev_id && a.sig_id == b.sig_id;
}

static int find(struct idu_key key)
{
    for (size_t i = 0; i < entry_count; ++i) {
        if (equal(entries[i].item.point.key, key)) { return i; }
    }
    return -ENOENT;
}

static int capture(struct idu_key key)
{
    int i = find(key);
    if (i >= 0) { return i; }
    const struct idu_point_cfg *p = idu_points_find(key);
    if (!p) { return -ENOENT; }
    if (entry_count == ARRAY_SIZE(entries)) { return -ENOSPC; }
    struct idu_point_state state;
    int err = idu_points_read(key, &state);
    if (err) { return err; }
    i = entry_count++;
    entries[i] = (struct entry){.item.point = {.key = key, .width = p->width,
                                               .value = state.value}, .old = state};
    return i;
}

static bool context_valid(struct idu_process_ctx *ctx)
{
    return ctx && ctx->transaction == entries && owner == k_current_get();
}

int idu_txn_get(struct idu_process_ctx *ctx, struct idu_key key,
                bool old, struct idu_value *value)
{
    if (!context_valid(ctx) || !value) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    transaction_quality |= entries[i].old.quality & IDU_QUALITY_SIMULATED;
    *value = old ? entries[i].old.value : entries[i].item.point.value;
    return 0;
}

int idu_txn_get_state(struct idu_process_ctx *ctx, struct idu_key key,
                       struct idu_point_state *state)
{
    if (!context_valid(ctx) || !state) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    *state = entries[i].old;
    if (entries[i].dirty) {
        state->value = entries[i].item.point.value;
        state->quality = IDU_QUALITY_GOOD | ctx->request->quality_flags;
        state->error = 0;
    }
    transaction_quality |= state->quality & IDU_QUALITY_SIMULATED;
    return 0;
}

bool idu_txn_contains(struct idu_process_ctx *ctx, struct idu_key key)
{
    if (!context_valid(ctx)) { return false; }
    for (size_t i = 0; i < ctx->request->count; ++i) {
        if (equal(ctx->request->items[i].point.key, key)) { return true; }
    }
    return false;
}

int idu_txn_emit(struct idu_process_ctx *ctx, struct idu_key key,
                 const struct idu_value *value, uint8_t flags)
{
    if (!context_valid(ctx) || !value || flags & ~IDU_OP_MASK) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    entries[i].item.point.value = *value;
    entries[i].item.flags = flags;
    entries[i].dirty = true;
    return 0;
}

int idu_process_execute(const struct idu_process_request *request,
                        struct idu_process_result *result)
{
    if (result) { *result = (struct idu_process_result){.id = request ? request->id : 0, .error = -EINVAL}; }
    if (!request || !result || !request->count ||
        request->count > CONFIG_IDU_PROCESS_MAX_OUTPUTS || (unsigned)request->operation > IDU_OP_SAMPLE ||
        (request->quality_flags & ~IDU_QUALITY_SIMULATED) ||
        k_is_in_isr()) { return -EINVAL; }
    *result = (struct idu_process_result){.id = request->id};
    if (k_mutex_lock(&process_lock, request->operation == IDU_OP_SAMPLE ? K_FOREVER : K_NO_WAIT)) { result->error = -EBUSY; return -EBUSY; }
    if (owner == k_current_get()) {
        k_mutex_unlock(&process_lock); result->error = -EDEADLK; return -EDEADLK;
    }
    owner = k_current_get();
    entry_count = 0;
    transaction_quality = request->quality_flags;
    struct idu_process_ctx ctx = {.request = request, .transaction = entries};
    int err = 0;
    for (size_t n = 0; n < request->count; ++n) {
        const struct idu_process_item *item = &request->items[n];
        const struct idu_point_cfg *p = idu_points_find(item->point.key);
        if (!p || item->flags & ~IDU_OP_MASK || find(item->point.key) >= 0) {
            err = p ? -EINVAL : -ENOENT; goto out;
        }
        int i = capture(item->point.key);
        if (i < 0) { err = i; goto out; }
        if (request->operation == IDU_OP_WRITE || request->operation == IDU_OP_SAMPLE) {
            if (item->point.width != p->width || item->point.error) { err = -EINVAL; goto out; }
            entries[i].item = *item;
            entries[i].dirty = true;
        }
    }
    /* All inputs are visible before any callback. Derived outputs never recurse. */
    for (size_t n = 0; n < request->count; ++n) {
        const struct idu_point_cfg *p = idu_points_find(request->items[n].point.key);
        if (p->handler && (request->operation != IDU_OP_READ || p->read_callback)) {
            err = p->handler(&ctx, p->key);
            if (err) { goto out; }
        }
    }
    for (size_t i = 0; i < entry_count; ++i) {
        if (!entries[i].dirty) { continue; }
        const struct idu_point_cfg *p = idu_points_find(entries[i].item.point.key);
        if (p->source == IDU_SOURCE_CONSTANT ||
            (p->source == IDU_SOURCE_PARAMETER && !(p->param_flags & IDU_PARAM_WRITE))) {
            err = -EACCES; goto out;
        }
        err = idu_points_check_value(p, &entries[i].item.point.value);
        if (err) { goto out; }
    }
    /* Bounded groups preserve per-device parameter transactions. Explicit
     * per-output results describe effects when a later device rejects a write. */
    bool done[CONFIG_IDU_PROCESS_MAX_OUTPUTS] = {0};
    for (size_t i = 0; i < entry_count; ++i) {
        if (!entries[i].dirty || done[i]) { continue; }
        const struct idu_point_cfg *p = idu_points_find(entries[i].item.point.key);
        uint8_t flags = entries[i].item.flags;
        size_t count = 0;
        for (size_t j = i; j < entry_count; ++j) {
            const struct idu_point_cfg *q = idu_points_find(entries[j].item.point.key);
            if (!entries[j].dirty || done[j] || entries[j].item.flags != flags ||
                (q->source == IDU_SOURCE_PARAMETER) != (p->source == IDU_SOURCE_PARAMETER) ||
                (p->source == IDU_SOURCE_PARAMETER && q->key.dev_id != p->key.dev_id)) { continue; }
            group[count++] = entries[j].item.point;
            done[j] = true;
        }
        int rc = p->source == IDU_SOURCE_PARAMETER ? idu_params_apply(group, count, flags) :
                 (flags & IDU_OP_NO_DM) ? 0 : idu_points_commit_sample(group, count,
                     transaction_quality, request->operation == IDU_OP_SAMPLE ?
                     request->sampled_at : k_uptime_get());
        for (size_t n = 0; n < count; ++n) {
            result->points[result->count].key = group[n].key;
            result->points[result->count++].error = rc;
        }
        if (rc) {
            err = rc;
            for (size_t n = 0; n < entry_count; ++n) {
                if (entries[n].dirty && !done[n]) {
                    result->points[result->count].key = entries[n].item.point.key;
                    result->points[result->count++].error = -ECANCELED;
                }
            }
            break;
        }
    }
out:
    result->error = err;
    owner = NULL;
    k_mutex_unlock(&process_lock);
    return err;
}

int idu_process_submit(const struct idu_process_request *request,
                       idu_process_done_fn done, void *user)
{
    if (!request || !done || !request->count || request->count > CONFIG_IDU_PROCESS_MAX_OUTPUTS ||
        (unsigned)request->operation > IDU_OP_TIMER || k_is_in_isr()) { return -EINVAL; }
    struct async_job job = {.request = *request, .done = done, .user = user};
    return k_msgq_put(&process_jobs, &job, K_NO_WAIT);
}

int idu_process_sample(const struct idu_point_batch *batch)
{
    /* Single zbus consumer owns this scratch space. */
    static struct idu_process_request request;
    static struct idu_process_result result;
    if (!batch || !batch->count || batch->count > ARRAY_SIZE(request.items)) { return -EINVAL; }
    request = (struct idu_process_request){.operation = IDU_OP_SAMPLE,
        .count = batch->count, .quality_flags = batch->flags, .sampled_at = batch->sampled_at};
    for (size_t i = 0; i < batch->count; ++i) {
        request.items[i].point = batch->points[i];
        request.items[i].flags = IDU_OP_NO_COLLECT | IDU_OP_NO_STORE;
    }
    return idu_process_execute(&request, &result);
}

int idu_process_read_at(struct idu_key key, uint16_t port_id, uint8_t function,
                        uint16_t address, uint16_t count, struct idu_point_state *state)
{
    const struct idu_point_cfg *p = idu_points_find(key);
    if (!p || !state) { return -EINVAL; }
    if (p->handler && p->read_callback) {
        struct idu_process_request request = {.operation = IDU_OP_READ, .port_id = port_id,
                                               .function = function, .address = address, .register_count = count,
                                               .count = 1, .items[0].point.key = key};
        struct idu_process_result result;
        int err = idu_process_execute(&request, &result);
        if (err) { return err; }
    }
    return idu_points_read(key, state);
}

int idu_process_read(struct idu_key key, uint16_t port_id, struct idu_point_state *state)
{
    return idu_process_read_at(key, port_id, 0, 0, 0, state);
}

int idu_process_init_device(uint16_t dev_id)
{
    const struct idu_equip_cfg *cfg = idu_points_config();
    if (!cfg) { return -EACCES; }
    struct idu_process_request request = {.operation = IDU_OP_INIT};
    struct idu_process_result result;
    for (size_t i = 0; i < cfg->point_count; ++i) {
        const struct idu_point_cfg *p = &cfg->points[i];
        if ((dev_id && p->key.dev_id != dev_id) || !p->handler) { continue; }
        request.count = 1;
        request.items[0].point.key = p->key;
        int err = idu_process_execute(&request, &result);
        if (err) { return err; }
    }
    return 0;
}

int idu_process_timer_start(struct idu_key key, uint32_t delay_ms, uint32_t period_ms)
{
    const struct idu_point_cfg *p = idu_points_find(key);
    if (!p || !p->handler) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    int slot = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(timers); ++i) {
        if (!timers[i].active) {
            timers[i] = (struct business_timer){.key = key, .active = true,
                .due = k_uptime_get() + delay_ms, .period = period_ms, .revision = ++timer_revision};
            slot = i; break;
        }
    }
    k_mutex_unlock(&timer_lock);
    return slot;
}

int idu_process_timer_cancel(int slot)
{
    if (slot < 0 || slot >= ARRAY_SIZE(timers)) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    timers[slot].active = false;
    timers[slot].revision = ++timer_revision;
    k_mutex_unlock(&timer_lock);
    return 0;
}

int idu_process_save_reboot(uint32_t delay_ms)
{
    if (delay_ms < 100) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    reboot_at = k_uptime_get() + delay_ms;
    k_mutex_unlock(&timer_lock);
    return 0;
}

__weak void idu_process_reboot(void) { sys_reboot(SYS_REBOOT_COLD); }

static void process_jobs_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct async_job job;
    struct idu_process_result result;
    while (true) {
        if (!k_msgq_get(&process_jobs, &job, K_MSEC(10))) {
            idu_process_execute(&job.request, &result);
            job.done(&result, job.user);
        }
        for (size_t i = 0; i < ARRAY_SIZE(timers); ++i) {
            k_mutex_lock(&timer_lock, K_FOREVER);
            bool due = timers[i].active && k_uptime_get() >= timers[i].due;
            struct idu_key key = timers[i].key;
            uint32_t revision = timers[i].revision;
            k_mutex_unlock(&timer_lock);
            if (due) {
                memset(&job.request, 0, sizeof(job.request));
                job.request.operation = IDU_OP_TIMER;
                job.request.count = 1;
                job.request.items[0].point.key = key;
                int err = idu_process_execute(&job.request, &result);
                k_mutex_lock(&timer_lock, K_FOREVER);
                if (timers[i].revision == revision) {
                    timers[i].active = err == -EBUSY || timers[i].period != 0;
                    timers[i].due = k_uptime_get() + (err == -EBUSY ? 10 : timers[i].period);
                }
                k_mutex_unlock(&timer_lock);
            }
        }
        k_mutex_lock(&timer_lock, K_FOREVER);
        bool reboot = reboot_at && k_uptime_get() >= reboot_at;
        if (reboot) { reboot_at = 0; }
        k_mutex_unlock(&timer_lock);
        if (reboot && !idu_params_flush()) { idu_process_reboot(); }
        idu_params_flush_due();
    }
}
K_THREAD_DEFINE(idu_jobs_tid, 6144, process_jobs_thread, NULL, NULL, NULL, 6, 0, 0);
