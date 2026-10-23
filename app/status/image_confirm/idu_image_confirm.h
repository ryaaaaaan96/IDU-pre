#ifndef IDU_IMAGE_CONFIRM_H
#define IDU_IMAGE_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

struct idu_image_confirm {
    int64_t started, first, previous;
    bool initialized, confirmed;
};

/* Call only after each complete acquisition + forwarding + health cycle.
 * A failed or stalled cycle must never confirm a trial image. */
int idu_image_confirm_checkpoint(struct idu_image_confirm *state, int64_t now, bool healthy);

#endif
