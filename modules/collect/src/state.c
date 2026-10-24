#include "state.h"
#include <errno.h>
#include <limits.h>

static const struct dp_equip_cfg *equipment;
static struct {
    struct co_device_status status;
    const struct co_device_policy *policy;
    size_t first_init, first_poll, cursor;
    int64_t due, exception_since;
    uint32_t replies;
} devices[CONFIG_DP_MAX_DEVICES];
static bool armed;
static int64_t next_poll[CONFIG_DP_POINTS_MAX_POLLS];
static bool disabled[CONFIG_DP_POINTS_MAX_POLLS];
static int (*offline_callback)(uint16_t, int, int64_t);
K_MUTEX_DEFINE(state_lock);

static int index_of(uint16_t dev)
{
    if (!equipment) { return -1; }
    for (size_t i = 0; i < equipment->device_count; ++i) {
        if (equipment->devices[i].dev_id == dev) { return i; }
    }
    return -1;
}

void co_state_set_offline_callback(int (*callback)(uint16_t, int, int64_t))
{
    offline_callback = callback;
}

static void notify(const struct co_device_status *status, int64_t offline_at)
{
    if (offline_at >= 0 && offline_callback) {
        (void)offline_callback(status->dev_id, -ETIMEDOUT, offline_at);
    }
    (void)zbus_chan_pub(&co_device_events, status, K_NO_WAIT);
}

size_t co_state_next_init(uint16_t dev, size_t begin)
{
    for (size_t i = begin; i < equipment->collect_count; ++i) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(equipment, i, &scratch);
        if (poll->dev_id == dev && poll->init_only) { return i; }
    }
    return SIZE_MAX;
}

void co_state_bind(const struct dp_equip_cfg *cfg, uint8_t bus, bool simulated)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    equipment = cfg;
    for (size_t i = 0; i < cfg->device_count; ++i) {
        devices[i].status.dev_id = cfg->devices[i].dev_id;
        devices[i].status.bus_id = cfg->devices[i].bus_id;
        if (cfg->devices[i].bus_id == bus) { devices[i].status.simulated = simulated; }
    }
    k_mutex_unlock(&state_lock);
}

int co_set_device_policy(uint16_t dev, const struct co_device_policy *policy)
{
    if (!policy || !policy->exception_ms || policy->exception_ms > INT32_MAX ||
        !policy->probe_ms || policy->probe_ms > INT32_MAX || k_is_in_isr()) { return -EINVAL; }
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i < 0 || armed) { k_mutex_unlock(&state_lock); return i < 0 ? -ENOENT : -EBUSY; }
    devices[i].policy = policy;
    devices[i].first_init = co_state_next_init(dev, 0);
    devices[i].first_poll = SIZE_MAX;
    for (size_t j = 0; j < equipment->collect_count; ++j) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(equipment, j, &scratch);
        if (poll->dev_id == dev && !poll->init_only && poll->period_ms) {
            devices[i].first_poll = j; break;
        }
    }
    devices[i].cursor = devices[i].first_init;
    devices[i].status.state = devices[i].cursor == SIZE_MAX ? CO_DEVICE_ONLINE : CO_DEVICE_INITIALIZING;
    devices[i].due = 0;
    k_mutex_unlock(&state_lock);
    return 0;
}

void co_state_arm(void)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    armed = true;
    k_mutex_unlock(&state_lock);
}

int co_state_get(uint16_t dev, struct co_device_status *status)
{
    if (!status) { return -EINVAL; }
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i >= 0) { *status = devices[i].status; }
    k_mutex_unlock(&state_lock);
    return i < 0 ? -EINVAL : 0;
}

void co_state_record(uint16_t dev, int error, struct co_outcome outcome)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i < 0) { k_mutex_unlock(&state_lock); return; }
    struct co_device_status *status = &devices[i].status;
    status->error = error;
    if (error) { ++status->failures; status->last_error = error; }
    else { ++status->successes; status->last_success = k_uptime_get(); }
    if (outcome.replied) {
        ++devices[i].replies;
        status->online = true;
        status->last_response = k_uptime_get();
        if (!devices[i].policy || status->state == CO_DEVICE_EXCEPTION) {
            status->state = CO_DEVICE_ONLINE;
        } else if (status->state == CO_DEVICE_OFFLINE) {
            devices[i].cursor = devices[i].first_init;
            status->state = devices[i].cursor == SIZE_MAX ? CO_DEVICE_ONLINE : CO_DEVICE_INITIALIZING;
            devices[i].due = 0;
        }
    }
    struct co_device_status event = *status;
    k_mutex_unlock(&state_lock);
    notify(&event, -1);
}

static void schedule_locked(uint16_t dev, struct co_schedule *schedule)
{
    *schedule = (struct co_schedule){.cursor = SIZE_MAX, .due = INT64_MAX};
    int i = index_of(dev);
    if (i >= 0 && devices[i].policy) {
        *schedule = (struct co_schedule){.managed = true, .state = devices[i].status.state,
            .cursor = devices[i].cursor, .due = devices[i].due,
            .retries = devices[i].policy->retries, .replies = devices[i].replies};
        if (schedule->state == CO_DEVICE_EXCEPTION) {
            schedule->due = devices[i].exception_since + devices[i].policy->exception_ms;
        }
    }
}

void co_state_schedule(uint16_t dev, struct co_schedule *schedule)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    schedule_locked(dev, schedule);
    k_mutex_unlock(&state_lock);
}

static void offline(size_t i)
{
    devices[i].status.state = CO_DEVICE_OFFLINE;
    devices[i].status.online = false;
    devices[i].cursor = devices[i].first_init != SIZE_MAX ? devices[i].first_init : devices[i].first_poll;
    devices[i].due = k_uptime_get() + devices[i].policy->probe_ms;
}

void co_state_tick(void)
{
    if (!equipment) { return; }
    for (size_t i = 0; i < equipment->device_count; ++i) {
        k_mutex_lock(&state_lock, K_FOREVER);
        bool expired = devices[i].policy && devices[i].status.state == CO_DEVICE_EXCEPTION &&
            k_uptime_get() - devices[i].exception_since >= devices[i].policy->exception_ms;
        int64_t offline_at = expired ? k_uptime_get() : -1;
        if (expired) { offline(i); }
        struct co_device_status event = devices[i].status;
        k_mutex_unlock(&state_lock);
        if (expired) { notify(&event, offline_at); }
    }
}

static void co_state_complete(uint16_t dev, const struct co_schedule *before,
                       size_t first, size_t rows, int error, struct co_outcome outcome)
{
    if (!before->managed) { return; }
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i < 0) { k_mutex_unlock(&state_lock); return; }
    bool was_offline = devices[i].status.state == CO_DEVICE_OFFLINE;
    enum co_device_online phase = before->state;
    if (outcome.missing && devices[i].replies != before->replies) {
        outcome.missing = false;
        outcome.replied = true;
    }
    if (error) { devices[i].status.error = error; devices[i].status.last_error = error; }
    if (phase == CO_DEVICE_INITIALIZING || phase == CO_DEVICE_OFFLINE) {
        if (phase == CO_DEVICE_OFFLINE && devices[i].first_init == SIZE_MAX && outcome.replied) {
            devices[i].cursor = SIZE_MAX;
            devices[i].status.state = CO_DEVICE_ONLINE;
            devices[i].due = 0;
        } else if (!error) {
            devices[i].cursor = co_state_next_init(dev, first + rows);
            devices[i].status.state = devices[i].cursor == SIZE_MAX ? CO_DEVICE_ONLINE : CO_DEVICE_INITIALIZING;
            devices[i].due = 0;
        } else if (outcome.missing || (phase == CO_DEVICE_OFFLINE && !outcome.replied)) {
            offline(i);
        } else {
            devices[i].status.state = CO_DEVICE_INITIALIZING;
            devices[i].cursor = devices[i].first_init;
            devices[i].due = k_uptime_get() + devices[i].policy->probe_ms;
        }
    } else if (outcome.missing && devices[i].status.state != CO_DEVICE_EXCEPTION) {
        devices[i].status.state = CO_DEVICE_EXCEPTION;
        devices[i].exception_since = k_uptime_get();
    }
    int64_t offline_at = !was_offline && devices[i].status.state == CO_DEVICE_OFFLINE ? k_uptime_get() : -1;
    struct co_device_status event = devices[i].status;
    k_mutex_unlock(&state_lock);
    notify(&event, offline_at);
}

void co_state_poll_enable(size_t block, bool enabled)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    disabled[block] = !enabled;
    next_poll[block] = k_uptime_get();
    k_mutex_unlock(&state_lock);
}

bool co_state_poll_due(size_t block, int64_t now)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    bool due = !disabled[block] && next_poll[block] <= now;
    k_mutex_unlock(&state_lock);
    return due;
}

void co_state_poll_plan(size_t block, struct co_poll_plan *plan)
{
    struct co_cfg scratch;
    const struct co_cfg *poll = dp_config_collect(equipment, block, &scratch);
    *plan = (struct co_poll_plan){.due = INT64_MAX, .deadline = INT64_MAX};
    k_mutex_lock(&state_lock, K_FOREVER);
    /* 在同一临界区内处理调度快照与表行期限。 */
    schedule_locked(poll->dev_id, &plan->snapshot);
    const struct co_schedule *schedule = &plan->snapshot;
    plan->retries = schedule->retries;
    if (schedule->managed && (schedule->state == CO_DEVICE_INITIALIZING ||
                              schedule->state == CO_DEVICE_OFFLINE)) {
        plan->eligible = block == schedule->cursor;
        plan->synchronous = true;
        plan->due = schedule->due;
        if (schedule->state == CO_DEVICE_OFFLINE) { plan->retries = 0; }
    } else {
        plan->eligible = !disabled[block] && !poll->init_only;
        plan->due = next_poll[block];
        if (schedule->managed && schedule->state == CO_DEVICE_EXCEPTION) {
            plan->deadline = schedule->due;
        }
    }
    k_mutex_unlock(&state_lock);
}

void co_state_poll_complete(size_t block, size_t rows, const struct co_poll_plan *plan,
                            int error, struct co_outcome outcome)
{
    struct co_cfg scratch;
    const struct co_cfg *poll = dp_config_collect(equipment, block, &scratch);
    uint16_t dev = poll->dev_id;
    uint32_t period = poll->period_ms;
    co_state_complete(dev, &plan->snapshot, block, rows, error, outcome);

    k_mutex_lock(&state_lock, K_FOREVER);
    int64_t now = k_uptime_get();
    for (size_t i = 0; i < rows; ++i) { next_poll[block + i] = now + period; }
    int d = index_of(dev);
    if (plan->synchronous && d >= 0 && devices[d].status.state == CO_DEVICE_ONLINE) {
        for (size_t i = 0; i < equipment->collect_count; ++i) {
            poll = dp_config_collect(equipment, i, &scratch);
            if (poll->dev_id == dev) { next_poll[i] = now; }
        }
    }
    k_mutex_unlock(&state_lock);
}

void co_state_initialized(uint16_t dev, int error)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i < 0 || !devices[i].policy) { k_mutex_unlock(&state_lock); return; }
    bool was_offline = devices[i].status.state == CO_DEVICE_OFFLINE;
    if (error) { devices[i].status.error = error; devices[i].status.last_error = error; }
    if (!error) { devices[i].status.state = CO_DEVICE_ONLINE; devices[i].cursor = SIZE_MAX; }
    else if (error == -ETIMEDOUT || error == -EBADMSG) { offline(i); }
    else {
        devices[i].status.state = CO_DEVICE_INITIALIZING;
        devices[i].cursor = devices[i].first_init;
        devices[i].due = k_uptime_get() + devices[i].policy->probe_ms;
    }
    int64_t offline_at = !was_offline && devices[i].status.state == CO_DEVICE_OFFLINE ? k_uptime_get() : -1;
    struct co_device_status event = devices[i].status;
    k_mutex_unlock(&state_lock);
    notify(&event, offline_at);
}

int co_clear_device_errors(uint16_t dev)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    k_mutex_lock(&state_lock, K_FOREVER);
    int i = index_of(dev);
    if (i >= 0) {
        devices[i].status.error = 0;
        devices[i].status.last_error = 0;
        devices[i].status.successes = 0;
        devices[i].status.failures = 0;
    }
    k_mutex_unlock(&state_lock);
    return i < 0 ? -ENOENT : 0;
}
