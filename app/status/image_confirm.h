/**
 * @file
 * @brief 试运行镜像的健康确认接口。
 *
 * 仅在完整的采样、转发和健康检查周期结束后调用。
 *
 * 失败或停滞的周期不得确认试运行镜像。
 * 仅供 status 内部使用；业务通过 inc/status.h 的 status_progress() 推进确认。
 */
#ifndef IMAGE_CONFIRM_H
#define IMAGE_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 试运行镜像的跨周期确认状态，由确认接口维护。
 */
struct image_confirm {
    int64_t started, first, previous;
    bool initialized, confirmed;
};

/**
 * @brief 在完整健康周期后检查并推进试运行镜像确认。
 * @param[in,out] state 首次使用前清零、跨周期保留的确认状态。
 * @param[in] now 当前系统运行时间，单位为毫秒，须非负。
 * @param[in] healthy 本周期的健康和存储条件是否满足。
 * @retval 0 检查通过，镜像可能仍在累计健康时长。
 * @retval -EINVAL 状态指针为空或时间为负。
 * @retval -ETIMEDOUT 时间早于起点或确认窗口已到期。
 * @return 其他错误码表示底层镜像确认失败。
 */
int image_confirm_checkpoint(struct image_confirm *state, int64_t now, bool healthy);

#endif
