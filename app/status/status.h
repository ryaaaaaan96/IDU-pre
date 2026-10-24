#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>
#include <stdbool.h>

/* Status owns this cadence; it does not indicate sampling progress. */
#define STATUS_LED_HALF_PERIOD_MS 100

enum status_phase {
    STATUS_UNCONFIGURED,
    STATUS_STARTING,
    STATUS_RUN, /* Initialization complete; acquisition cycles may start. */
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
/* Consistent RAM diagnosis, including faults reported from an ISR. */
int status_application_fault_get(struct status_application_fault *snapshot);

struct status_degradation {
    uint16_t remote_offline, remote_initializing;
    int storage_error;
};
/* Protocol diagnostics do not clear terminal faults or stop local sampling. */
void status_update_degradation(uint16_t offline, uint16_t initializing, int storage_error);
struct status_degradation status_degradation_get(void);

/* One private product status, initialized once per boot. Repeated init returns -EALREADY.
 * Fast acquisition phases only record progress; LED policy lives in status.c.
 * Faults retain their first phase/error and cannot be cleared without reset.
 */
/* Initializes status, LED and (unless APP_DEBUG_SKIP_WATCHDOG) watchdog.
 * The application enters STATUS_RUN
 * after all startup services succeed. */
int status_init(void);
void status_set_phase(enum status_phase phase);
int status_progress(void);
enum status_phase status_phase_get(void);
/* ISR-safe, retains the first error. Continue returns 0; terminal returns error
 * if reset did not take control. Watchdog failure can escalate a held fault. */
int status_report_fault(enum status_fault fault,
                        enum status_phase phase, int error);

#endif
