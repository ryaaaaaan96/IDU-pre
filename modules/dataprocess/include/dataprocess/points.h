/**
 * @file
 * @brief 测点值、状态和最小运行接口。
 *
 * 当前产品只运行本地同步读写：没有样本队列、业务线程、采集描述符或转发映射。
 */
#ifndef DP_POINTS_H
#define DP_POINTS_H

#include <dataprocess/reference_tables.h>

#define DP_POINT_WORDS CONFIG_DP_POINT_WORDS

typedef enum {
    DP_QUALITY_GOOD,
    DP_QUALITY_UNINITIALIZED,
    DP_QUALITY_COMM_ERROR,
    DP_QUALITY_STALE,
    DP_QUALITY_BAD_VALUE,
} dp_quality_t;

/** @brief 存储中的数值采用低字在前、主机字节序。 */
struct dp_value {
    uint16_t words[DP_POINT_WORDS];
};

struct dp_point_state {
    struct dp_value value;
    int64_t sampled_at;
    uint32_t generation;
    int error;
    uint16_t quality;
};

struct dp_table {
    uint16_t device_id;
    const DM_SIG_CFG_S *points;
    size_t point_count;
    struct dp_point_state *states; /**< 与 points 等长，独立且常驻的可写状态数组。 */
    struct dp_table *next; /**< DP 维护的登记链表；应用不读写。 */
};

/**
 * @brief 启动时逐张登记点表；参数分区由设备树 dp-params 别名指定。
 * @note 行数组只读，表描述和 states 可写且常驻；不同表 device_id 不同。
 * @note 登记在启动线程完成；运行后不再登记。参数挂载后禁止增加持久参数。
 */
int dp_register_table(struct dp_table *table);

/**
 * @brief 同步写入单个点，字节长度必须等于配置长度。
 *
 * 常量点返回 -EACCES；命令点只执行回调，不保存触发值；参数点同步提交 NVS。
 */
int dp_set_para(uint16_t dev_id, uint16_t sig_id, uint8_t size, const uint8_t *bytes);

/** @brief 直接读取，不运行写回调；成功时返回传入容量。 */
int dp_get_para(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *bytes);

#endif
