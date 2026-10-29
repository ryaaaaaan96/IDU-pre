/**
 * @file
 * @brief DP 模块内部共享的最小接口。
 */
#ifndef DP_INTERNAL_H
#define DP_INTERNAL_H

#include <dataprocess/dataprocess.h>
#include <time.h>
#include <zephyr/kernel.h>
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

/** @brief 模块私有的测点运行数据与持久化状态。 */
typedef struct {
    value_t value;
    value_t *saved_value;  /**< 持久点的保存副本；普通点为空，由模块私有管理。 */
    uint32_t updated_at_s; /**< UNIX 秒，UTC；未校时和配置初值为 0。 */
    bool saved_valid;      /**< 保存副本是否可信，失败后清零，重新保存成功后置位。 */
    bool dirty;            /**< 持久值尚未保存。 */
} dp_point_data_t;

/* 私有设备目录；节点与点数据一次分配，只在模块内访问。 */
typedef struct dp_registered_table {
    uint16_t device_id;
    const dp_point_config_t *points;
    size_t point_count;
    struct dp_registered_table *next;
    struct dp_storage *storage;
    bool factory_resetting; /**< 设备锁保护；出厂恢复后禁止 Flash 保存，RAM 读写不受影响。 */
    dp_point_data_t data[];
} dp_registered_table_t;

dp_registered_table_t *dp_tables(void);

/* 锁顺序：设备/保存锁 → 点值快照锁；回调不持有这两把锁。 */
int dp_params_init(void);
/** @brief 关联并挂载设备槽位；调用方已确认含持久点，并持有设备锁。 */
int dp_params_attach(dp_registered_table_t *device);
/** @brief 仅加载点值；缺失返回 -ENOENT，记录头不兼容返回 -ESTALE，读取错误原样返回。 */
int dp_params_load(dp_registered_table_t *device, size_t index);
int dp_params_write(dp_registered_table_t *device, size_t index, const value_t *value);
/**
 * @brief 删除 Flash 参数及映射，不修改 RAM 点表；调用方持有设备锁。
 * @note DP_DEVICE_ID_ALL 擦除总分区，调用前须禁止全部保存，成功后必须重启。
 */
int dp_params_remove(uint16_t device_id);

#endif
