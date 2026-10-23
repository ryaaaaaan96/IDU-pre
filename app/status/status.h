#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>
#include <stdbool.h>
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "status/image_confirm/image_confirm.h"
#endif

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

struct application_status {
    enum status_phase phase;
    enum status_phase fault_phase;
    int last_error;
    bool led_on;
    /* Diagnostic degradation, independent of the local watchdog fault latch. */
    uint16_t remote_offline, remote_initializing;
    int storage_error;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct image_confirm image_confirm;
#endif
};

/* One product status, owned by the application thread. Zero-initialize once.
 * Fast acquisition phases only record progress; LED policy lives in status.c.
 * Faults retain their first phase/error and cannot be cleared without reset.
 */
/* Initializes status, LED and watchdog. The application enters STATUS_RUN
 * after all startup services succeed. */
int status_init(struct application_status *status);
void status_set_phase(struct application_status *status, enum status_phase phase);
int status_progress(struct application_status *status);
void status_fail(struct application_status *status, enum status_phase phase, int error);

#endif
