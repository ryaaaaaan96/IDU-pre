#include "idu_boot.h"
#include <errno.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(idu_boot, LOG_LEVEL_INF);

int idu_boot_health_checkpoint(struct idu_boot_health *state, int64_t now, bool healthy)
{
    if (!state || now < 0) { return -EINVAL; }
    if (!state->initialized) {
        state->initialized = true;
        state->started = now;
        state->first = state->previous = -1;
        state->confirmed = boot_is_img_confirmed();
    }
    if (state->confirmed) { return 0; }
    if (now < state->started || now - state->started >= 60000) { return -ETIMEDOUT; }
    if (!healthy) { state->first = state->previous = -1; return 0; }
    if (state->first < 0 || now < state->previous || now - state->previous > 1500) {
        state->first = now;
    }
    state->previous = now;
    if (now - state->first < 10000) { return 0; }
    int err = boot_write_img_confirmed();
    if (err) { LOG_ERR("Trial image confirmation failed: %d", err); return err; }
    state->confirmed = true;
    LOG_INF("Trial image confirmed after 10 seconds of healthy acquisition/forwarding");
    return 0;
}
