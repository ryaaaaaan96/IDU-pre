#include <data_process.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <dataprocess/params.h>
#include <dataprocess/reference_internal.h>
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dp_reference, LOG_LEVEL_WRN);

static size_t byte_count(const struct dp_point_cfg *point)
{
    return point->size_bytes ? point->size_bytes :
           point->type == DP_U8 ? 1 : point->width * 2;
}

/* Explicit byte assembly also works on a big-endian Zephyr target. */
#if defined(CONFIG_DP_PROCESS)
static void unpack(struct dp_value *value, const uint8_t *bytes, size_t count)
{
    memset(value, 0, sizeof(*value));
    for (size_t i = 0; i < count; ++i) {
        value->words[i / 2] |= (uint16_t)bytes[i] << (8 * (i % 2));
    }
}

#endif

static void pack(uint8_t *bytes, const struct dp_value *value, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = value->words[i / 2] >> (8 * (i % 2));
    }
}

static int lookup(uint16_t dev, uint16_t sig, size_t size, bool exact,
                  const struct dp_point_cfg **point, struct dp_point_cfg *scratch)
{
    if (!dev || !sig || !size) { return -EINVAL; }
    *point = dp_find((struct dp_key){dev, sig}, scratch);
    if (!*point) { return -ENOENT; }
    size_t needed = byte_count(*point);
    return (exact ? size != needed : size < needed) ? -EMSGSIZE : 0;
}

int32_t dp_get_sig_data_type(uint16_t dev, uint16_t sig,
                           uint16_t *type, uint16_t *size)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!type || !size) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point = dp_find((struct dp_key){dev, sig}, &point_scratch);
    if (!point) { return -ENOENT; }
    static const uint16_t types[] = {
        DATA_TYPE_U16, DATA_TYPE_S16, DATA_TYPE_U32, DATA_TYPE_S32,
        DATA_TYPE_U16ARRAY, DATA_TYPE_U8,
    };
    *type = point->reference_data_type ? point->reference_data_type : types[point->type];
    *size = byte_count(point);
    return 0;
}

int32_t dp_get_sig_value(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!bytes) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(dev, sig, size, false, &point, &point_scratch);
    if (err) { return err; }
    struct dp_point_state state;
    err = dp_read(point->key, &state);
    if (!err) { pack(bytes, &state.value, byte_count(point)); }
    return err ? err : size;
}

#if defined(CONFIG_DP_PROCESS)
/* The native process lock serializes callbacks. A thread identity check keeps
 * helper calls in other threads (or ISRs) from borrowing the active context.
 */
struct callback_scope {
    struct dp_process_ctx *ctx;
    k_tid_t thread;
};
struct read_scope {
    struct dp_key key;
    struct dp_value value;
    k_tid_t thread;
    bool captured;
    int status;
};
static struct callback_scope *active_callback;
static struct read_scope *active_read;
static struct k_spinlock scope_lock;
K_MUTEX_DEFINE(reference_lock);
/* Protected by reference_lock; never kept across an asynchronous submit. */
struct reference_buffers {
    struct dp_process_request request;
    struct dp_process_result result;
};
static struct reference_buffers *reference;

static struct dp_process_ctx *callback_context(void)
{
    if (k_is_in_isr()) { return NULL; }
    k_spinlock_key_t lock = k_spin_lock(&scope_lock);
    struct dp_process_ctx *ctx = active_callback &&
        active_callback->thread == k_current_get() ? active_callback->ctx : NULL;
    k_spin_unlock(&scope_lock, lock);
    return ctx;
}

struct dp_process_ctx *dp_reference_context(void) { return callback_context(); }

static int enter(void)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (callback_context()) { return -EDEADLK; }
    int err = k_mutex_lock(&reference_lock, K_FOREVER);
    if (err) { return err; }
    reference = k_malloc(sizeof(*reference));
    if (!reference) { k_mutex_unlock(&reference_lock); return -ENOMEM; }
    return 0;
}

static void leave(void)
{
    k_free(reference); reference = NULL;
    k_mutex_unlock(&reference_lock);
}

static uint8_t *data_bytes(DATA_SET_S *data)
{
    return data->sig_size <= sizeof(data->p_value.str) ?
           data->p_value.str : data->p_value.pst;
}

static int prepare(struct dp_process_item *item, uint16_t dev, uint16_t sig,
                   size_t size, const uint8_t *bytes)
{
    if (!bytes) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(dev, sig, size, true, &point, &point_scratch);
    if (err) { return err; }
    if (point->source == DP_SOURCE_CONSTANT ||
        (point->source == DP_SOURCE_PARAMETER && !(point->param_flags & DP_PARAM_WRITE))) {
        return -EACCES;
    }
    *item = (struct dp_process_item){.point = {.key = point->key, .width = point->width}};
    unpack(&item->point.value, bytes, size);
    return dp_check_value(point, &item->point.value);
}

int32_t dp_set_sig_value(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{
    int err = enter();
    if (err) { return err; }
    reference->request = (struct dp_process_request){.operation = DP_OP_WRITE, .count = 1};
    err = prepare(&reference->request.items[0], dev, sig, size, bytes);
    if (!err) { err = dp_process_execute(&reference->request, &reference->result); }
    leave();
    return err;
}

int32_t dp_fast_para_set(PARA_DATA_S *data, PARA_SET_STATUS status)
{
    int err = enter();
    if (err) { return err; }
    if (!data || !data->p_set_data || !data->set_num ||
        data->set_num > CONFIG_DP_PROCESS_MAX_OUTPUTS) {
        err = -EINVAL; goto out;
    }
    if (data->type != DP_FAST_SET) { err = -ENOTSUP; goto out; }
    reference->request = (struct dp_process_request){.operation = DP_OP_WRITE,
        .port_id = data->port_id, .count = data->set_num};
    for (size_t i = 0; i < data->set_num; ++i) {
        DATA_SET_S *item = &data->p_set_data[i];
        if (item->extra.addr_type != FUNC_PARA_REG_TYPE_MB) { err = -ENOTSUP; goto out; }
        err = prepare(&reference->request.items[i], item->dev_id, item->sig_id,
                      item->sig_size, data_bytes(item));
        if (err) { goto out; }
        reference->request.items[i].reference_address = item->extra.reg_addr;
        reference->request.items[i].reference_address_valid = true;
    }
    err = dp_process_execute(&reference->request, &reference->result);
out:
    leave();
    if (!err && status) { status(data); }
    return err;
}

static int reference_read(struct dp_key key, uint16_t port, uint8_t function,
                         uint16_t address, uint16_t count, struct dp_point_state *state,
                         bool *response)
{
    if (dp_stopped()) { return -ECANCELED; }
    if (response) { *response = false; }
    if (!state) { return -EINVAL; }
    int err = enter();
    if (err) { return err; }
    struct read_scope scope = {.key = key, .thread = k_current_get()};
    reference->request = (struct dp_process_request){.operation = DP_OP_READ,
        .port_id = port, .function = function, .address = address,
        .register_count = count, .count = 1, .items[0].point.key = key};
    k_spinlock_key_t lock = k_spin_lock(&scope_lock);
    active_read = &scope;
    k_spin_unlock(&scope_lock, lock);
    err = dp_process_execute(&reference->request, &reference->result);
    lock = k_spin_lock(&scope_lock);
    active_read = NULL;
    k_spin_unlock(&scope_lock, lock);
    if (!err || (scope.captured && err == scope.status)) {
        int read_error = dp_read(key, state);
        if (read_error) { err = read_error; }
        else if (scope.captured) {
            state->value = scope.value;
            state->quality = (state->quality & DP_QUALITY_SIMULATED) | DP_QUALITY_GOOD;
            state->error = 0;
            state->sampled_at = k_uptime_get();
            if (response) { *response = true; }
        }
    }
    leave();
    return err;
}

int dp_reference_read_at(struct dp_key key, uint16_t port, uint8_t function,
                         uint16_t address, uint16_t count, struct dp_point_state *state)
{
    if (dp_stopped()) { return -ECANCELED; }
    int err = reference_read(key, port, function, address, count, state, NULL);
    /* Nonzero reference READ status is not a native successful transaction. */
    return err > 0 ? -EIO : err;
}

int32_t dp_fast_para_get(PARA_DATA_S *data)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!data || !data->p_set_data || data->set_num != 1) { return -EINVAL; }
    DATA_SET_S *item = data->p_set_data;
    uint8_t *bytes = data_bytes(item);
    if (!bytes) { return -EINVAL; }
    if (item->extra.addr_type != FUNC_PARA_REG_TYPE_MB) { return -ENOTSUP; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(item->dev_id, item->sig_id, item->sig_size, true, &point, &point_scratch);
    if (err) { return err; }
    struct dp_point_state state;
    bool response = false;
    err = point->reference_handler && point->read_callback ?
          reference_read(point->key, data->port_id, 0, item->extra.reg_addr, point->width, &state, &response) :
          dp_process_read_at(point->key, data->port_id, 0, item->extra.reg_addr, point->width, &state);
    if (err && !response) { return err; }
    int callback_status = err;
    uint16_t size;
    err = dp_get_sig_data_type(item->dev_id, item->sig_id, &item->data_type, &size);
    if (!err) { pack(bytes, &state.value, size); }
    return err ? err : callback_status ? callback_status : size;
}

static int helper_get(FUNC_PARA_S *para, bool old)
{
    struct dp_process_ctx *ctx = callback_context();
    if (!ctx) { return -EPERM; }
    if (!para || !para->pvalue) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(para->dev_id, para->sig_id, para->sig_size, false, &point, &point_scratch);
    if (err) { return err; }
    struct dp_value value;
    err = dp_txn_get(ctx, point->key, old, &value);
    if (!err) { pack(para->pvalue, &value, byte_count(point)); }
    return err;
}

int32_t dp_get_para_value(FUNC_PARA_S *para) { return helper_get(para, false); }
int32_t dp_get_para_old_value(FUNC_PARA_S *para) { return helper_get(para, true); }

int32_t dp_check_para_set(FUNC_PARA_S *para)
{
    struct dp_process_ctx *ctx = callback_context();
    if (!ctx) { return -EPERM; }
    if (!para) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(para->dev_id, para->sig_id, para->sig_size, true, &point, &point_scratch);
    if (err) { return err; }
    uint8_t flags;
    err = dp_txn_get_flags(ctx, point->key, &flags);
    /* Setting the existing flags also verifies that this entry is an output. */
    return err ? err : dp_txn_set_flags(ctx, point->key, flags);
}

int32_t dp_set_para_value_flag(FUNC_PARA_S *para, uint8_t flags)
{
    struct dp_process_ctx *ctx = callback_context();
    if (!ctx) { return -EPERM; }
    if (!para || flags & ~0x07U) { return -EINVAL; }
    struct dp_process_item item;
    int err = prepare(&item, para->dev_id, para->sig_id, para->sig_size, para->pvalue);
    if (err) { return err; }
    uint8_t previous = 0;
    err = dp_txn_get_flags(ctx, item.point.key, &previous);
    if (err && err != -ENOENT) { return err; }
    return dp_txn_emit(ctx, item.point.key, &item.point.value, previous | flags);
}

int32_t dp_set_para_value(FUNC_PARA_S *para) { return dp_set_para_value_flag(para, 0); }

int32_t dp_set_para_flag(uint16_t dev, uint16_t sig, uint8_t flags)
{
    struct dp_process_ctx *ctx = callback_context();
    if (!ctx) { return -EPERM; }
    if (flags & ~0x07U) { return -EINVAL; }
    struct dp_key key = {dev, sig};
    uint8_t previous;
    int err = dp_txn_get_flags(ctx, key, &previous);
    return err ? err : dp_txn_set_flags(ctx, key, previous | flags);
}

void dp_set_para_no_collect(uint16_t dev, uint16_t sig)
{
    (void)dp_set_para_flag(dev, sig, DP_OP_NO_COLLECT);
}

int32_t dp_set_para_only_dm(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{
    FUNC_PARA_S para = {.dev_id = dev, .sig_id = sig, .sig_size = size, .pvalue = bytes};
    return dp_set_para_value_flag(&para, DP_OP_NO_COLLECT);
}

int32_t dp_set_para_only_dm_no_store(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{
    FUNC_PARA_S para = {.dev_id = dev, .sig_id = sig, .sig_size = size, .pvalue = bytes};
    return dp_set_para_value_flag(&para, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static int invoke_global(struct dp_process_ctx *ctx, void *user)
{
    ARG_UNUSED(user);
    DM_PF_SIG_PROC callback = ctx->request->reference_callback;
    struct callback_scope scope = {.ctx = ctx, .thread = k_current_get()};
    k_spinlock_key_t lock = k_spin_lock(&scope_lock);
    struct callback_scope *previous = active_callback;
    active_callback = &scope;
    k_spin_unlock(&scope_lock, lock);
    int err = callback(NULL, NULL, NULL, NULL);
    lock = k_spin_lock(&scope_lock);
    active_callback = previous;
    k_spin_unlock(&scope_lock, lock);
    return err < 0 ? err : 0;
}

int dp_reference_invoke(DM_PF_SIG_PROC callback, bool timer)
{
    if (!callback) { return -EINVAL; }
    int err = enter();
    if (err) { return err; }
    reference->request = (struct dp_process_request){
        .operation = timer ? DP_OP_TIMER : DP_OP_INIT,
        .callback = invoke_global, .reference_callback = callback,
    };
    err = dp_process_execute(&reference->request, &reference->result);
    leave();
    return err;
}

static void reference_done(const struct dp_process_result *result, void *user)
{
    ARG_UNUSED(user);
    if (result->error) { LOG_WRN("Registered callback failed: %d", result->error); }
}

int dp_reference_submit(DM_PF_SIG_PROC callback, bool timer)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (!callback) { return -EINVAL; }
    if (callback_context()) { return -EDEADLK; }
    if (k_mutex_lock(&reference_lock, K_NO_WAIT)) { return -EBUSY; }
    reference = k_malloc(sizeof(*reference));
    if (!reference) { k_mutex_unlock(&reference_lock); return -ENOMEM; }
    reference->request = (struct dp_process_request){
        .operation = timer ? DP_OP_TIMER : DP_OP_INIT,
        .callback = invoke_global, .reference_callback = callback,
    };
    int err = dp_process_submit(&reference->request, reference_done, NULL);
    leave();
    return err;
}

int dp_reference_process(struct dp_process_ctx *ctx, struct dp_key key)
{
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point = dp_find(key, &point_scratch);
    if (!ctx || !point || !point->reference_handler) { return -EINVAL; }
    uint8_t bytes[DP_POINT_WORDS * 2], original[DP_POINT_WORDS * 2];
    struct dp_value value;
    int err = dp_txn_get(ctx, key, false, &value);
    if (err) { return err; }
    size_t size = byte_count(point);
    pack(bytes, &value, size);
    memcpy(original, bytes, size);
    uint16_t address = ctx->request->address;
    for (size_t i = 0; i < ctx->request->count; ++i) {
        const struct dp_process_item *input = &ctx->request->items[i];
        if (input->reference_address_valid && input->point.key.dev_id == key.dev_id &&
            input->point.key.sig_id == key.sig_id) {
            address = input->reference_address;
            break;
        }
    }
    FUNC_PARA_S item = {.dev_id = key.dev_id, .sig_id = key.sig_id,
        .sig_size = size, .extra.reg_addr = address, .pvalue = bytes};
    FUNC_PARA_ALL input = {.type = ctx->request->operation == DP_OP_READ ? DP_PARA_GET :
        ctx->request->operation == DP_OP_INIT ? DP_PARA_INIT : DP_PARA_SET,
        .port_id = ctx->request->port_id, .para_num = 1, .para_all = &item};
    struct callback_scope scope = {.ctx = ctx, .thread = k_current_get()};
    k_spinlock_key_t lock = k_spin_lock(&scope_lock);
    struct callback_scope *previous = active_callback;
    active_callback = &scope;
    k_spin_unlock(&scope_lock, lock);
    err = point->reference_handler(ctx->request->operation == DP_OP_TIMER ? NULL : &input,
                                   NULL, NULL, NULL);
    lock = k_spin_lock(&scope_lock);
    active_callback = previous;
    if (ctx->request->operation == DP_OP_READ && active_read &&
        active_read->thread == scope.thread && active_read->key.dev_id == key.dev_id &&
        active_read->key.sig_id == key.sig_id) {
        unpack(&active_read->value, bytes, size);
        active_read->captured = true;
        active_read->status = err;
    }
    k_spin_unlock(&scope_lock, lock);
    if (err < 0 || (err && ctx->request->operation == DP_OP_READ)) { return err; }
    if (ctx->request->operation != DP_OP_READ && memcmp(original, bytes, size)) {
        uint8_t flags;
        err = dp_txn_get_flags(ctx, key, &flags);
        if (err) { return err; }
        unpack(&value, bytes, size);
        return dp_txn_emit(ctx, key, &value, flags);
    }
    return 0;
}
#else
/* Keep the public byte API linkable in a store-only configuration. */
int32_t dp_set_sig_value(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{ (void)dev; (void)sig; (void)size; (void)bytes; return -ENOTSUP; }
int32_t dp_fast_para_set(PARA_DATA_S *data, PARA_SET_STATUS status)
{ (void)data; (void)status; return -ENOTSUP; }
int32_t dp_fast_para_get(PARA_DATA_S *data)
{ (void)data; return -ENOTSUP; }
int dp_reference_invoke(DM_PF_SIG_PROC callback, bool timer)
{ (void)callback; (void)timer; return -ENOTSUP; }
int dp_reference_submit(DM_PF_SIG_PROC callback, bool timer)
{ (void)callback; (void)timer; return -ENOTSUP; }
int dp_reference_process(struct dp_process_ctx *ctx, struct dp_key key)
{ (void)ctx; (void)key; return -ENOTSUP; }
int32_t dp_get_para_value(FUNC_PARA_S *para) { (void)para; return -ENOTSUP; }
int32_t dp_get_para_old_value(FUNC_PARA_S *para) { (void)para; return -ENOTSUP; }
int32_t dp_check_para_set(FUNC_PARA_S *para) { (void)para; return -ENOTSUP; }
int32_t dp_set_para_value(FUNC_PARA_S *para) { (void)para; return -ENOTSUP; }
int32_t dp_set_para_value_flag(FUNC_PARA_S *para, uint8_t flags)
{ (void)para; (void)flags; return -ENOTSUP; }
int32_t dp_set_para_flag(uint16_t dev, uint16_t sig, uint8_t flags)
{ (void)dev; (void)sig; (void)flags; return -ENOTSUP; }
int dp_defer_para_action(int (*action)(uint16_t), uint16_t argument)
{ (void)action; (void)argument; return -ENOTSUP; }
void dp_set_para_no_collect(uint16_t dev, uint16_t sig) { (void)dev; (void)sig; }
int32_t dp_set_para_only_dm(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{ (void)dev; (void)sig; (void)size; (void)bytes; return -ENOTSUP; }
int32_t dp_set_para_only_dm_no_store(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *bytes)
{ (void)dev; (void)sig; (void)size; (void)bytes; return -ENOTSUP; }
#endif

/* A copied, single-flight collector request survives an asynchronous completion.
 * On timeout its storage remains reserved until the backend completes, avoiding
 * dangling caller buffers and preventing a late ACK from completing a new job.
 */
static PARA_SET_FUNC reference_collector;
static struct k_spinlock collector_lock;
#if defined(CONFIG_DP_PROCESS)
struct collector_buffers {
    PARA_DATA_S request;
    DATA_SET_S items[CONFIG_DP_PROCESS_MAX_OUTPUTS];
    uint8_t bytes[CONFIG_DP_PROCESS_MAX_OUTPUTS][DP_POINT_WORDS * 2];
};
static struct collector_buffers *collector;
static bool collector_busy, collector_waiting, collector_done;
K_SEM_DEFINE(collector_complete, 0, 1);
#endif

int32_t dp_para_reg_set_func(PARA_SET_FUNC callback)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_spinlock_key_t lock = k_spin_lock(&collector_lock);
    int err = 0;
#if defined(CONFIG_DP_PROCESS)
    if (collector_busy) { err = -EBUSY; }
#endif
    if (!err && reference_collector && callback != reference_collector) { err = -EBUSY; }
    if (!err) { reference_collector = callback; }
    k_spin_unlock(&collector_lock, lock);
    return err;
}

#if defined(CONFIG_DP_PROCESS)
static void collected(PARA_DATA_S *data)
{
    struct collector_buffers *release = NULL;
    k_spinlock_key_t lock = k_spin_lock(&collector_lock);
    if (collector && data == &collector->request && collector_busy) {
        collector_done = true;
        if (collector_waiting) { k_sem_give(&collector_complete); }
        else { collector_busy = false; release = collector; collector = NULL; }
    }
    k_spin_unlock(&collector_lock, lock);
    k_free(release);
}

int dp_reference_collect(PARA_DATA_S *data)
{
    if (dp_stopped()) { return -ECANCELED; }
    if (!data || !data->p_set_data || !data->set_num ||
        data->set_num > ARRAY_SIZE(collector->items) || k_is_in_isr()) { return -EINVAL; }
    if (data->type != DP_FAST_SET && data->type != DP_TIMER_SET && data->type != DP_NO_DM_SET) { return -ENOTSUP; }
    k_spinlock_key_t lock = k_spin_lock(&collector_lock);
    PARA_SET_FUNC callback = reference_collector;
    int err = !callback ? -ENODEV : collector_busy ? -EBUSY : 0;
    if (!err) { collector_busy = collector_waiting = true; collector_done = false; }
    k_spin_unlock(&collector_lock, lock);
    if (err) { return err; }
    struct collector_buffers *buffer = k_malloc(sizeof(*buffer));
    if (!buffer) {
        lock = k_spin_lock(&collector_lock);
        collector_busy = collector_waiting = false;
        k_spin_unlock(&collector_lock, lock);
        return -ENOMEM;
    }
    lock = k_spin_lock(&collector_lock);
    collector = buffer;
    k_spin_unlock(&collector_lock, lock);
    k_sem_reset(&collector_complete);
    collector->request = *data;
    collector->request.p_set_data = collector->items;
    for (size_t i = 0; i < data->set_num; ++i) {
        const DATA_SET_S *item = &data->p_set_data[i];
        const uint8_t *bytes = item->sig_size > 4 ? item->p_value.pst : item->p_value.str;
        if (!bytes || !item->sig_size || item->sig_size > sizeof(collector->bytes[i]) ||
            item->extra.addr_type != FUNC_PARA_REG_TYPE_MB) { err = -EINVAL; break; }
        collector->items[i] = *item;
        if (item->sig_size > 4) {
            memcpy(collector->bytes[i], bytes, item->sig_size);
            collector->items[i].p_value.pst = collector->bytes[i];
        }
    }
    if (!err && dp_stopped()) { err = -ECANCELED; }
    bool accepted = false;
    if (!err) {
        err = callback(&collector->request, collected);
        accepted = !err;
        if (err > 0) { err = -EIO; }
        if (!err) { err = k_sem_take(&collector_complete, K_SECONDS(1)) ? -ETIMEDOUT : 0; }
    }
    lock = k_spin_lock(&collector_lock);
    collector_waiting = false;
    /* A backend error means rejection, with no later completion permitted. */
    struct collector_buffers *release = NULL;
    if (!accepted || err != -ETIMEDOUT || collector_done) {
        collector_busy = false; release = collector; collector = NULL;
    }
    k_spin_unlock(&collector_lock, lock);
    k_free(release);
    return err;
}

int32_t dp_set_para_not_dm(PARA_DATA_S *data, PARA_SET_STATUS status)
{
    struct dp_process_ctx *ctx = callback_context();
    int err = dp_reference_collect(data);
    if (err) { return err; }
    if (ctx) {
        for (size_t i = 0; i < data->set_num; ++i) {
            struct dp_key key = {data->p_set_data[i].dev_id, data->p_set_data[i].sig_id};
            uint8_t flags;
            if (!dp_txn_get_flags(ctx, key, &flags)) {
                (void)dp_txn_set_flags(ctx, key, flags | DP_OP_NO_DM | DP_OP_NO_COLLECT);
            }
        }
    }
    if (status) { status(data); }
    return 0;
}

int32_t dp_para_value_reg(uint16_t type, DATA_SET_S *data)
{
    struct dp_process_ctx *ctx = callback_context();
    if (!ctx) { return -EPERM; }
    if (!data || (type != PARA_VALUE_REG_OLD && type != PARA_VALUE_REG_NEW)) { return -EINVAL; }
    struct dp_point_cfg point_scratch;
    const struct dp_point_cfg *point;
    int err = lookup(data->dev_id, data->sig_id, data->sig_size, true, &point, &point_scratch);
    uint8_t *bytes = data_bytes(data);
    if (err || !bytes) { return err ? err : -EINVAL; }
    struct dp_value value;
    unpack(&value, bytes, data->sig_size);
    if (type == PARA_VALUE_REG_OLD) { return dp_txn_register_old(ctx, point->key, &value); }
    uint8_t flags = 0;
    (void)dp_txn_get_flags(ctx, point->key, &flags);
    return dp_txn_emit(ctx, point->key, &value, flags);
}
#else
int dp_reference_collect(PARA_DATA_S *data) { ARG_UNUSED(data); return -ENOTSUP; }
int32_t dp_set_para_not_dm(PARA_DATA_S *data, PARA_SET_STATUS status)
{ ARG_UNUSED(data); ARG_UNUSED(status); return -ENOTSUP; }
int32_t dp_para_value_reg(uint16_t type, DATA_SET_S *data)
{ ARG_UNUSED(type); ARG_UNUSED(data); return -ENOTSUP; }
#endif
