/**
 * @file
 * @brief DP 模块内部共享的最小接口。
 */
#ifndef DP_INTERNAL_H
#define DP_INTERNAL_H

#include <dataprocess/points.h>
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

/* 编码属于 NVS 格式，不能随公共类型枚举重排。 */
typedef enum {
    DP_U16 = 0,
    DP_S16 = 1,
    DP_U32 = 2,
    DP_S32 = 3,
    DP_WORDS = 4,
    DP_U8 = 5,
} dp_storage_type_t;

typedef enum {
    DP_SOURCE_CONSTANT,
    DP_SOURCE_LOCAL,
    DP_SOURCE_PARAMETER,
} dp_source_t;

typedef struct {
    dp_key_t key;
    dp_source_t source;
    dp_storage_type_t type;
    uint8_t width;
    uint8_t size_bytes;
    uint8_t flags;
    dp_write_callback_t write_callback;
    dp_read_callback_t read_callback;
    dp_value_t default_value;
    int64_t minimum;
    int64_t maximum;
} dp_point_cfg_t;

int dp_params_read(dp_key_t key, dp_point_data_t *state);
bool dp_params_mounted(void);
int dp_params_write(dp_key_t key, const dp_value_t *value);

/* 私有设备目录；节点与点数据一次分配，只在模块内访问。 */
typedef struct dp_registered_table {
    uint16_t device_id;
    const dp_point_def_t *points;
    size_t point_count;
    struct dp_registered_table *next;
    dp_point_data_t data[];
} dp_registered_table_t;

dp_registered_table_t *dp_tables(void);
int dp_describe_point(uint16_t dev, const dp_point_def_t *row, dp_point_cfg_t *point);
int dp_check_value(const dp_point_cfg_t *point, const dp_value_t *value);

#endif
