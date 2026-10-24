#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>
#include <stdbool.h>
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "status/image_confirm/image_confirm.h"
#endif

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

struct application_status {
    enum status_phase phase;
    enum status_phase fault_phase;
    int last_error;
    enum status_fault first_fault, terminal_fault;
    enum status_action action;
    int64_t fault_at_ms;
    /* Diagnostic degradation, independent of the local watchdog fault latch. */
    uint16_t remote_offline, remote_initializing;
    int storage_error;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
};

/* One product status with static lifetime. Zero-initialize once.
 * Fast acquisition phases only record progress; LED policy lives in status.c.
 * Faults retain their first phase/error and cannot be cleared without reset.
 */
/* Initializes status, LED and (unless APP_DEBUG_SKIP_WATCHDOG) watchdog.
 * The application enters STATUS_RUN
 * after all startup services succeed. */
int status_init(struct application_status *status);
void status_set_phase(struct application_status *status, enum status_phase phase);
int status_progress(struct application_status *status);
enum status_phase status_phase_get(struct application_status *status);
/* ISR-safe, retains the first error. Continue returns 0; terminal returns error
 * if reset did not take control. Watchdog failure can escalate a held fault. */
int status_report_fault(struct application_status *status, enum status_fault fault,
                        enum status_phase phase, int error);

#endif
