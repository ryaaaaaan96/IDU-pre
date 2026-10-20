#ifndef IDU_ACQUISITION_SERVICE_H
#define IDU_ACQUISITION_SERVICE_H

#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "acquisition/idu_boot.h"
#endif

enum idu_acquisition_state {
    IDU_ACQUISITION_UNCONFIGURED,
    IDU_ACQUISITION_STARTING,
    IDU_ACQUISITION_WAITING,
    IDU_ACQUISITION_SAMPLING,
    IDU_ACQUISITION_PUBLISHING,
    IDU_ACQUISITION_SERVICING_COMMUNICATION,
    IDU_ACQUISITION_CHECKING_HEALTH,
    IDU_ACQUISITION_FAULT_LOCKED,
};

/* Zero-initialize once. Only the application thread drives this state machine.
 * A fault records its original phase/error and remains locked until reset.
 */
struct idu_acquisition {
    enum idu_acquisition_state state;
    enum idu_acquisition_state fault_phase;
    int last_error;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct idu_boot_health boot;
#endif
};

int idu_acquisition_start(struct idu_acquisition *acquisition);
/* Complete one sampling/publication/communication cycle; main owns scheduling. */
int idu_acquisition_cycle(struct idu_acquisition *acquisition);

#endif
