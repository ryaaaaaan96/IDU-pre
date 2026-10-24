#ifndef IMAGE_CONFIRM_H
#define IMAGE_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

struct image_confirm {
    int64_t started, first, previous;
    bool initialized, confirmed;
};

/* 仅在完整的采样、转发和健康检查周期结束后调用。
 * 失败或停滞的周期不得确认试运行镜像。 */
int image_confirm_checkpoint(struct image_confirm *state, int64_t now, bool healthy);

#endif
