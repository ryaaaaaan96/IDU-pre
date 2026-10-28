/**
 * @file
 * @brief DP 模块内部共享的最小接口。
 */
#ifndef DP_INTERNAL_H
#define DP_INTERNAL_H

#include <dataprocess/dataprocess.h>
#include <time.h>
#include <zephyr/sys/clock.h>

/** @brief 非零墙钟偏移视为已校时；越界或未校时返回未知时间 0。 */
static inline uint32_t dp_timestamp_s(void)
{
    struct timespec offset, now;
    sys_clock_getrtoffset(&offset);
    if ((!offset.tv_sec && !offset.tv_nsec) || sys_clock_gettime(SYS_CLOCK_REALTIME, &now) ||
        now.tv_sec <= 0 || (uint64_t)now.tv_sec > UINT32_MAX) {
        return 0;
    }
    return (uint32_t)now.tv_sec;
}

/* 私有设备目录；节点与点数据一次分配，只在模块内访问。 */
typedef struct dp_registered_table {
    uint16_t device_id;
    const dp_point_config_t *points;
    size_t point_count;
    struct dp_registered_table *next;
    dp_point_data_t data[];
} dp_registered_table_t;

dp_registered_table_t *dp_tables(void);

#endif
