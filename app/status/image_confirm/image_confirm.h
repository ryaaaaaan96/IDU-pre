#ifndef IMAGE_CONFIRM_H
#define IMAGE_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

struct image_confirm {
    int64_t started, first, previous;
    bool initialized, confirmed;
};

/* Call only after each complete acquisition + forwarding + health cycle.
 * A failed or stalled cycle must never confirm a trial image. */
int image_confirm_checkpoint(struct image_confirm *state, int64_t now, bool healthy);

#endif
