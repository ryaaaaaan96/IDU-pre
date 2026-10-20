#ifndef IDU_ACQUISITION_SERVICE_H
#define IDU_ACQUISITION_SERVICE_H

#include "status/idu_status.h"

/* Zero-initialize once. Only the application thread drives this state machine.
 * A fault records its original phase/error and remains locked until reset.
 */
int idu_acquisition_start(struct idu_status *status);
/* Complete one sampling/publication/communication cycle; main owns scheduling. */
int idu_acquisition_cycle(struct idu_status *status);

#endif
