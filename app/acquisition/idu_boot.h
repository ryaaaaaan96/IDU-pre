#ifndef IDU_BOOT_H
#define IDU_BOOT_H

#include <stdbool.h>
#include <stdint.h>

struct idu_boot_health {
    int64_t started, first, previous;
    bool initialized, confirmed;
};

/* Call only after each complete acquisition + forwarding + health cycle.
 * A failed or stalled cycle must never confirm a trial image. */
int idu_boot_health_checkpoint(struct idu_boot_health *state, int64_t now, bool healthy);

#endif
