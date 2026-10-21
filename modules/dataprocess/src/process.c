#include <dataprocess/process.h>
#include <dataprocess/params.h>
#include <dataprocess/reference_internal.h>
#include <errno.h>
#include <string.h>
#if defined(CONFIG_DP_PARAMS)
#include <zephyr/sys/reboot.h>
#endif

struct entry {
    struct dp_process_item item;
    struct dp_point_state old;
    bool dirty;
};
static struct entry entries[CONFIG_DP_PROCESS_MAX_OUTPUTS];
static size_t entry_count;
static uint16_t transaction_quality;
static k_tid_t owner;
static const struct dp_process_request *active_request;
static struct k_spinlock context_lock;
K_MUTEX_DEFINE(process_lock);
static struct dp_point_update group[CONFIG_DP_PROCESS_MAX_OUTPUTS];
struct async_job {
    struct dp_process_request request;
    dp_process_done_fn done;
    void *user;
};
K_MSGQ_DEFINE(process_jobs, sizeof(struct async_job), CONFIG_DP_PROCESS_QUEUE_DEPTH, 4);
K_MUTEX_DEFINE(submission_lock);
static struct async_job submission;
struct business_timer {
    struct dp_key key;
    int64_t due;
    uint32_t period, revision;
    bool active, reserved;
};
static uint32_t timer_revision;
static struct business_timer timers[CONFIG_DP_PROCESS_TIMERS];
K_MUTEX_DEFINE(timer_lock);
K_MUTEX_DEFINE(lifecycle_lock);
static bool lifecycle_started, lifecycle_starting;
#if defined(CONFIG_DP_PARAMS)
static int64_t reboot_at;
#endif

static bool equal(struct dp_key a, struct dp_key b)
{
    return a.dev_id == b.dev_id && a.sig_id == b.sig_id;
}

static int find(struct dp_key key)
{
    for (size_t i = 0; i < entry_count; ++i) {
        if (equal(entries[i].item.point.key, key)) { return i; }
    }
    return -ENOENT;
}

static int capture(struct dp_key key)
{
    int i = find(key);
    if (i >= 0) { return i; }
    struct dp_point_cfg p_scratch;
    const struct dp_point_cfg *p = dp_find(key, &p_scratch);
    if (!p) { return -ENOENT; }
    if (entry_count == ARRAY_SIZE(entries)) { return -ENOSPC; }
    struct dp_point_state state;
    int err = dp_read(key, &state);
    if (err) { return err; }
    i = entry_count++;
    entries[i] = (struct entry){.item.point = {.key = key, .width = p->width,
                                               .value = state.value}, .old = state};
    return i;
}

const struct dp_process_request *dp_process_current_request(void)
{
    k_spinlock_key_t lock = k_spin_lock(&context_lock);
    const struct dp_process_request *request = owner == k_current_get() ? active_request : NULL;
    k_spin_unlock(&context_lock, lock);
    return request;
}

static bool context_valid(struct dp_process_ctx *ctx)
{
    return ctx && ctx->transaction == entries && owner == k_current_get();
}

int dp_txn_get(struct dp_process_ctx *ctx, struct dp_key key,
                bool old, struct dp_value *value)
{
    if (!context_valid(ctx) || !value) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    transaction_quality |= entries[i].old.quality & DP_QUALITY_SIMULATED;
    *value = old ? entries[i].old.value : entries[i].item.point.value;
    return 0;
}

int dp_txn_get_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t *flags)
{
    if (!context_valid(ctx) || !flags) { return -EINVAL; }
    int i = find(key);
    if (i < 0) { return i; }
    *flags = entries[i].item.flags;
    return 0;
}

int dp_txn_set_flags(struct dp_process_ctx *ctx, struct dp_key key, uint8_t flags)
{
    if (!context_valid(ctx) || flags & ~DP_OP_MASK) { return -EINVAL; }
    int i = find(key);
    if (i < 0 || !entries[i].dirty) { return -ENOENT; }
    entries[i].item.flags = flags;
    return 0;
}

int dp_txn_get_state(struct dp_process_ctx *ctx, struct dp_key key,
                       struct dp_point_state *state)
{
    if (!context_valid(ctx) || !state) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    *state = entries[i].old;
    if (entries[i].dirty) {
        state->value = entries[i].item.point.value;
        state->quality = DP_QUALITY_GOOD | ctx->request->quality_flags;
        state->error = 0;
    }
    transaction_quality |= state->quality & DP_QUALITY_SIMULATED;
    return 0;
}

bool dp_txn_contains(struct dp_process_ctx *ctx, struct dp_key key)
{
    if (!context_valid(ctx)) { return false; }
    for (size_t i = 0; i < ctx->request->count; ++i) {
        if (equal(ctx->request->items[i].point.key, key)) { return true; }
    }
    return false;
}

int dp_txn_emit(struct dp_process_ctx *ctx, struct dp_key key,
                 const struct dp_value *value, uint8_t flags)
{
    if (!context_valid(ctx) || !value || flags & ~DP_OP_MASK) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    entries[i].item.point.value = *value;
    entries[i].item.flags = flags;
    entries[i].dirty = true;
    return 0;
}

int dp_txn_register_old(struct dp_process_ctx *ctx, struct dp_key key, const struct dp_value *value)
{
    if (!context_valid(ctx) || !value) { return -EINVAL; }
    int i = capture(key);
    if (i < 0) { return i; }
    entries[i].old.value = *value;
    return 0;
}

int dp_process_execute(const struct dp_process_request *request,
                        struct dp_process_result *result)
{
    if (result) { *result = (struct dp_process_result){.id = request ? request->id : 0, .error = -EINVAL}; }
    if (!request || !result || (!request->count && !request->callback) ||
        (request->callback && (request->count ||
            (request->operation != DP_OP_INIT && request->operation != DP_OP_TIMER))) ||
        request->count > CONFIG_DP_PROCESS_MAX_OUTPUTS || (unsigned)request->operation > DP_OP_SAMPLE ||
        (request->quality_flags & ~DP_QUALITY_SIMULATED) ||
        k_is_in_isr()) { return -EINVAL; }
    *result = (struct dp_process_result){.id = request->id};
    if (k_mutex_lock(&process_lock, request->operation == DP_OP_SAMPLE ? K_FOREVER : K_NO_WAIT)) { result->error = -EBUSY; return -EBUSY; }
    if (owner == k_current_get()) {
        k_mutex_unlock(&process_lock); result->error = -EDEADLK; return -EDEADLK;
    }
    k_spinlock_key_t context_key = k_spin_lock(&context_lock);
    owner = k_current_get();
    active_request = request;
    k_spin_unlock(&context_lock, context_key);
    entry_count = 0;
    transaction_quality = request->quality_flags;
    struct dp_process_ctx ctx = {.request = request, .transaction = entries};
    int err = 0;
    for (size_t n = 0; n < request->count; ++n) {
        const struct dp_process_item *item = &request->items[n];
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(item->point.key, &p_scratch);
        if (!p || item->flags & ~DP_OP_MASK || find(item->point.key) >= 0) {
            err = p ? -EINVAL : -ENOENT; goto out;
        }
        int i = capture(item->point.key);
        if (i < 0) { err = i; goto out; }
        if (request->operation == DP_OP_WRITE || request->operation == DP_OP_SAMPLE) {
            if (item->point.width != p->width || item->point.error) { err = -EINVAL; goto out; }
            entries[i].item = *item;
            entries[i].dirty = true;
        }
    }
    if (request->callback) {
        err = request->callback(&ctx, request->user);
        if (err) { goto out; }
    }
    /* All inputs are visible before any callback. Derived outputs never recurse. */
    for (size_t n = 0; n < request->count; ++n) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(request->items[n].point.key, &p_scratch);
        if (p->handler && (request->operation != DP_OP_READ || p->read_callback)) {
            err = p->handler(&ctx, p->key);
            if (err) { goto out; }
        }
    }
    for (size_t i = 0; i < entry_count; ++i) {
        if (!entries[i].dirty) { continue; }
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(entries[i].item.point.key, &p_scratch);
        if (p->source == DP_SOURCE_CONSTANT ||
            (p->source == DP_SOURCE_PARAMETER && !(p->param_flags & DP_PARAM_WRITE))) {
            err = -EACCES; goto out;
        }
        err = dp_check_value(p, &entries[i].item.point.value);
        if (err) { goto out; }
    }
    /* Bounded groups preserve per-device parameter transactions. Explicit
     * per-output results describe effects when a later device rejects a write. */
    bool done[CONFIG_DP_PROCESS_MAX_OUTPUTS] = {0};
    for (size_t i = 0; i < entry_count; ++i) {
        if (!entries[i].dirty || done[i]) { continue; }
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(entries[i].item.point.key, &p_scratch);
        uint8_t flags = entries[i].item.flags;
        size_t count = 0;
        for (size_t j = i; j < entry_count; ++j) {
            struct dp_point_cfg q_scratch;
            const struct dp_point_cfg *q = dp_find(entries[j].item.point.key, &q_scratch);
            if (!entries[j].dirty || done[j] || entries[j].item.flags != flags ||
                (q->source == DP_SOURCE_PARAMETER) != (p->source == DP_SOURCE_PARAMETER) ||
                (p->source == DP_SOURCE_PARAMETER && q->key.dev_id != p->key.dev_id)) { continue; }
            group[count++] = entries[j].item.point;
            done[j] = true;
        }
        int rc;
        if (p->source == DP_SOURCE_PARAMETER) {
#if defined(CONFIG_DP_PARAMS)
            rc = request->operation == DP_OP_SAMPLE &&
                 (flags & (DP_OP_NO_COLLECT | DP_OP_NO_STORE)) ==
                 (DP_OP_NO_COLLECT | DP_OP_NO_STORE) &&
                 (p->param_flags & DP_PARAM_FORWARD) ?
                 dp_params_sample(group, count, transaction_quality, request->sampled_at) :
                 dp_params_apply(group, count, flags);
#else
            rc = -ENOTSUP;
#endif
        } else {
            rc = (flags & DP_OP_NO_DM) ? 0 : dp_commit_sample(group, count,
                     transaction_quality, request->operation == DP_OP_SAMPLE ?
                     request->sampled_at : k_uptime_get());
        }
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
    err = dp_reference_finish(&ctx, err);
    result->error = err;
    context_key = k_spin_lock(&context_lock);
    owner = NULL;
    active_request = NULL;
    k_spin_unlock(&context_lock, context_key);
    k_mutex_unlock(&process_lock);
    return err;
}

int dp_process_submit(const struct dp_process_request *request,
                       dp_process_done_fn done, void *user)
{
    if (!request || !done || (!request->count && !request->callback) ||
        (request->callback && (request->count ||
            (request->operation != DP_OP_INIT && request->operation != DP_OP_TIMER))) ||
        request->count > CONFIG_DP_PROCESS_MAX_OUTPUTS ||
        (unsigned)request->operation > DP_OP_TIMER || k_is_in_isr()) { return -EINVAL; }
    if (k_mutex_lock(&submission_lock, K_NO_WAIT)) { return -EBUSY; }
    memcpy(&submission.request, request, sizeof(*request));
    submission.done = done;
    submission.user = user;
    int err = k_msgq_put(&process_jobs, &submission, K_NO_WAIT);
    k_mutex_unlock(&submission_lock);
    return err;
}

int dp_process_sample(const struct dp_point_batch *batch)
{
    /* Single zbus consumer owns this scratch space. */
    static struct dp_process_request request;
    static struct dp_process_result result;
    if (!batch || !batch->count || batch->count > ARRAY_SIZE(request.items)) { return -EINVAL; }
    request = (struct dp_process_request){.operation = DP_OP_SAMPLE,
        .count = batch->count, .quality_flags = batch->flags, .sampled_at = batch->sampled_at};
    for (size_t i = 0; i < batch->count; ++i) {
        request.items[i].point = batch->points[i];
        request.items[i].flags = DP_OP_NO_COLLECT | DP_OP_NO_STORE;
    }
    return dp_process_execute(&request, &result);
}

int dp_process_read_at(struct dp_key key, uint16_t port_id, uint8_t function,
                        uint16_t address, uint16_t count, struct dp_point_state *state)
{
    struct dp_point_cfg p_scratch;
    const struct dp_point_cfg *p = dp_find(key, &p_scratch);
    if (!p || !state) { return -EINVAL; }
    if (p->reference_handler && p->read_callback) {
        return dp_reference_read_at(key, port_id, function, address, count, state);
    }
    if (p->handler && p->read_callback) {
        struct dp_process_request request = {.operation = DP_OP_READ, .port_id = port_id,
                                               .function = function, .address = address, .register_count = count,
                                               .count = 1, .items[0].point.key = key};
        struct dp_process_result result;
        int err = dp_process_execute(&request, &result);
        if (err) { return err; }
    }
    return dp_read(key, state);
}

int dp_process_read(struct dp_key key, uint16_t port_id, struct dp_point_state *state)
{
    return dp_process_read_at(key, port_id, 0, 0, 0, state);
}

static int initialize_key(struct dp_key key)
{
    struct dp_process_request request = {
        .operation = DP_OP_INIT, .count = 1, .items[0].point.key = key,
    };
    struct dp_process_result result;
    return dp_process_execute(&request, &result);
}

int dp_process_init_device(uint16_t dev_id)
{
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg) { return -EACCES; }
    for (size_t i = 0; i < cfg->point_count; ++i) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, i, &p_scratch);
        if ((dev_id && p->key.dev_id != dev_id) || !p->handler) { continue; }
        int err = initialize_key(p->key);
        if (err) { return err; }
    }
    return 0;
}

static int timer_timeout_ms(k_timeout_t timeout, uint32_t *milliseconds)
{
    if (timeout.ticks < 0) {
        return -EINVAL;
    }
    uint64_t ms = k_ticks_to_ms_ceil64(timeout.ticks);
    if (ms > UINT32_MAX) {
        return -ERANGE;
    }
    *milliseconds = ms;
    return 0;
}

int dp_process_start(const struct dp_process_plan *plan)
{
    if (!plan || k_is_in_isr() || (plan->init_count && !plan->init_keys) ||
        (plan->timer_count && !plan->timers) ||
        (plan->initialize_all && plan->init_count) ||
        plan->timer_count > ARRAY_SIZE(timers)) {
        return -EINVAL;
    }
    if (k_current_get() == &k_sys_work_q.thread) {
        return -EWOULDBLOCK;
    }
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg) {
        return -EACCES;
    }
    if (plan->init_count > cfg->point_count) {
        return -EINVAL;
    }
    uint32_t delays[CONFIG_DP_PROCESS_TIMERS], periods[CONFIG_DP_PROCESS_TIMERS];
    for (size_t i = 0; i < plan->init_count; ++i) {
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(plan->init_keys[i], &p_scratch);
        if (!p || !p->handler) {
            return -EINVAL;
        }
        for (size_t j = 0; j < i; ++j) {
            if (equal(plan->init_keys[i], plan->init_keys[j])) {
                return -EEXIST;
            }
        }
    }
    for (size_t i = 0; i < plan->timer_count; ++i) {
        const struct dp_timer_spec *spec = &plan->timers[i];
        struct dp_point_cfg p_scratch;
        const struct dp_point_cfg *p = dp_find(spec->key, &p_scratch);
        if (!p || !p->handler) {
            return -EINVAL;
        }
        int err = timer_timeout_ms(spec->delay, &delays[i]);
        if (!err) {
            err = timer_timeout_ms(spec->period, &periods[i]);
        }
        if (err) {
            return err;
        }
        for (size_t j = 0; j < i; ++j) {
            if (equal(spec->key, plan->timers[j].key)) {
                return -EEXIST;
            }
        }
    }
    if (k_mutex_lock(&lifecycle_lock, K_NO_WAIT)) {
        return -EBUSY;
    }
    if (lifecycle_started || lifecycle_starting) {
        int err = lifecycle_started ? -EALREADY : -EBUSY;
        k_mutex_unlock(&lifecycle_lock);
        return err;
    }
    int slots[CONFIG_DP_PROCESS_TIMERS];
    size_t reserved = 0;
    k_mutex_lock(&timer_lock, K_FOREVER);
    for (size_t i = 0; i < ARRAY_SIZE(timers) && reserved < plan->timer_count; ++i) {
        if (!timers[i].active && !timers[i].reserved) {
            slots[reserved++] = i;
        }
    }
    if (reserved != plan->timer_count) {
        k_mutex_unlock(&timer_lock);
        k_mutex_unlock(&lifecycle_lock);
        return -ENOSPC;
    }
    for (size_t i = 0; i < reserved; ++i) {
        timers[slots[i]].reserved = true;
    }
    k_mutex_unlock(&timer_lock);
    lifecycle_starting = true;
    int err = plan->initialize_all ? dp_process_init_device(0) : 0;
    for (size_t i = 0; !err && i < plan->init_count; ++i) {
        err = initialize_key(plan->init_keys[i]);
    }
    k_mutex_lock(&timer_lock, K_FOREVER);
    int64_t now = k_uptime_get();
    for (size_t i = 0; i < reserved; ++i) {
        struct business_timer *timer = &timers[slots[i]];
        timer->reserved = false;
        if (!err) {
            *timer = (struct business_timer){
                .key = plan->timers[i].key, .due = now + delays[i],
                .period = periods[i], .revision = ++timer_revision, .active = true,
            };
        }
    }
    k_mutex_unlock(&timer_lock);
    lifecycle_started = !err;
    lifecycle_starting = false;
    k_mutex_unlock(&lifecycle_lock);
    return err;
}

int dp_process_timer_start(struct dp_key key, uint32_t delay_ms, uint32_t period_ms)
{
    struct dp_point_cfg p_scratch;
    const struct dp_point_cfg *p = dp_find(key, &p_scratch);
    if (!p || !p->handler) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    int slot = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(timers); ++i) {
        if (!timers[i].active && !timers[i].reserved) {
            timers[i] = (struct business_timer){.key = key, .active = true,
                .due = k_uptime_get() + delay_ms, .period = period_ms, .revision = ++timer_revision};
            slot = i; break;
        }
    }
    k_mutex_unlock(&timer_lock);
    return slot;
}

int dp_process_timer_cancel(int slot)
{
    if (slot < 0 || slot >= ARRAY_SIZE(timers)) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    if (timers[slot].reserved) {
        k_mutex_unlock(&timer_lock);
        return -EBUSY;
    }
    timers[slot].active = false;
    timers[slot].revision = ++timer_revision;
    k_mutex_unlock(&timer_lock);
    return 0;
}

int dp_process_save_reboot(uint32_t delay_ms)
{
#if defined(CONFIG_DP_PARAMS)
    if (delay_ms < 100) { return -EINVAL; }
    k_mutex_lock(&timer_lock, K_FOREVER);
    reboot_at = k_uptime_get() + delay_ms;
    k_mutex_unlock(&timer_lock);
    return 0;
#else
    ARG_UNUSED(delay_ms);
    return -ENOTSUP;
#endif
}

#if defined(CONFIG_DP_PARAMS)
__weak void dp_process_reboot(void) { sys_reboot(SYS_REBOOT_COLD); }
#endif

static void process_jobs_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct async_job job;
    struct dp_process_result result;
    while (true) {
        if (!k_msgq_get(&process_jobs, &job, K_MSEC(10))) {
            dp_process_execute(&job.request, &result);
            job.done(&result, job.user);
        }
        for (size_t i = 0; i < ARRAY_SIZE(timers); ++i) {
            k_mutex_lock(&timer_lock, K_FOREVER);
            bool due = timers[i].active && k_uptime_get() >= timers[i].due;
            struct dp_key key = timers[i].key;
            uint32_t revision = timers[i].revision;
            k_mutex_unlock(&timer_lock);
            if (due) {
                memset(&job.request, 0, sizeof(job.request));
                job.request.operation = DP_OP_TIMER;
                job.request.count = 1;
                job.request.items[0].point.key = key;
                int err = dp_process_execute(&job.request, &result);
                k_mutex_lock(&timer_lock, K_FOREVER);
                if (timers[i].revision == revision) {
                    timers[i].active = err == -EBUSY || timers[i].period != 0;
                    timers[i].due = k_uptime_get() + (err == -EBUSY ? 10 : timers[i].period);
                }
                k_mutex_unlock(&timer_lock);
            }
        }
#if defined(CONFIG_DP_PARAMS)
        k_mutex_lock(&timer_lock, K_FOREVER);
        bool reboot = reboot_at && k_uptime_get() >= reboot_at;
        if (reboot) { reboot_at = 0; }
        k_mutex_unlock(&timer_lock);
        if (reboot && !dp_params_flush()) { dp_process_reboot(); }
        dp_params_flush_due();
#endif
        dp_reference_drain_events();
    }
}
K_THREAD_DEFINE(dp_jobs_tid, CONFIG_DP_PROCESS_STACK_SIZE,
                process_jobs_thread, NULL, NULL, NULL, 6, 0, 0);
