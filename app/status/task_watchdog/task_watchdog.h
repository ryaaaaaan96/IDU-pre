#ifndef STATUS_TASK_WATCHDOG_H
#define STATUS_TASK_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

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
/* Caller-owned, zero-initialize once and keep alive. Fields are private state;
 * access through the snapshot APIs, not by modifying this object. */
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
/* Configuration values are copied; the task and name remain caller-owned.
 * Register/feed/sleep are thread APIs. Snapshot queries are also ISR-safe. */
/* Register immediately starts the deadline. Active duplicates return -EALREADY.
 * No fixed slots or allocation: each caller supplies its own context.
 * Failures leave the context unchanged. A global
 * latched failure rejects register/feed/sleep with -ECANCELED until reset. */
int status_register(struct status_task *task, const struct status_task_config *config);
int status_feed(struct status_task *task);
/* Cancel this task's deadline only, keep STATUS_TASK_SLEEP_FOREVER and diagnostic
 * values. Idempotent; status_feed cannot resume it. Register again to resume. */
int status_sleep_forever(struct status_task *task);
/* -ENOENT if never registered / no task fault. No query mutates state. */
int status_task_get(const struct status_task *task, struct status_task_snapshot *snapshot);
int status_fault_get(struct status_fault_snapshot *snapshot);

#endif
