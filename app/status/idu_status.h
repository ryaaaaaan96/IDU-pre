#ifndef IDU_STATUS_H
#define IDU_STATUS_H

#include <stdint.h>
#include <stdbool.h>
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "status/idu_image_confirm.h"
#endif

enum idu_status_phase {
    IDU_STATUS_UNCONFIGURED,
    IDU_STATUS_STARTING,
    IDU_STATUS_WAITING,
    IDU_STATUS_SAMPLING,
    IDU_STATUS_PUBLISHING,
    IDU_STATUS_SERVICING_COMMUNICATION,
    IDU_STATUS_CHECKING_HEALTH,
    IDU_STATUS_FAULT_LOCKED,
};

struct idu_status {
    enum idu_status_phase phase;
    enum idu_status_phase fault_phase;
    int last_error;
    bool led_on;
    /* Diagnostic degradation, independent of the local watchdog fault latch. */
    uint16_t remote_offline, remote_initializing;
    int storage_error;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct idu_image_confirm image_confirm;
#endif
};

/* One product status, owned by the application thread. Zero-initialize once.
 * Fast acquisition phases only record progress; LED policy lives in status.c.
 * Faults retain their first phase/error and cannot be cleared without reset.
 */
int idu_status_start(struct idu_status *status);
void idu_status_set_phase(struct idu_status *status, enum idu_status_phase phase);
int idu_status_progress(struct idu_status *status);
void idu_status_fail(struct idu_status *status, enum idu_status_phase phase, int error);
uint32_t idu_status_timeout_ms(void);

#endif
