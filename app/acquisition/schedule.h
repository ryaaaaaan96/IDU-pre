/** @file @brief 采集周期的绝对时间推进。 */
#ifndef ACQUISITION_SCHEDULE_H
#define ACQUISITION_SCHEDULE_H
#include <stdint.h>
#include <limits.h>

#define ACQUISITION_IDLE_DEADLINE INT64_MAX

/* 首次以实际采样时刻建立时间轴，随后跳过超期周期，不补采。 */
static inline int64_t acquisition_next_deadline(int64_t deadline, uint32_t period, int64_t now)
{
    if (deadline == 0) {
        return now + period;
    }
    return deadline + ((now - deadline) / period + 1) * period;
}
#endif
