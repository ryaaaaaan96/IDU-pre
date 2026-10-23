#ifndef STATUS_TASK_WATCHDOG_H
#define STATUS_TASK_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#define STATUS_TASK_ACTIVE UINT32_C(1)
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
/* Caller-owned, zero-initialize once and keep alive. Fields are private state;
 * access through the snapshot APIs, not by modifying this object. */
struct status_task {
    struct status_task_snapshot state;
    int64_t deadline_ticks;
    int channel;
    bool registered;
};
struct status_fault_snapshot {
    struct status_task_snapshot task;
    int64_t occurred_ms;
    int error;
    int feed_error;
};
/* Configuration values are copied; the task and name remain caller-owned.
 * Register/feed are thread APIs. Snapshot queries are also ISR-safe. */
int status_register(struct status_task *task, const struct status_task_config *config);
int status_feed(struct status_task *task);
int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot);
int status_fault_get(struct status_fault_snapshot *snapshot);

#endif
