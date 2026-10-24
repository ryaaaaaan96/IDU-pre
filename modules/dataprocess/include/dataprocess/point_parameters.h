/**
 * @file
 * @brief 按国家配置的测点参数查询接口。
 */
#ifndef DP_POINT_PARAMETERS_H
#define DP_POINT_PARAMETERS_H

#include <dataprocess/reference_tables.h>

#define DP_COUNTRY_COMMON 0U

/**
 * @brief 可选国家差异覆盖：value/restore 已填入处理表的通用配置。
 *
 * 范围是否启用仍由处理行的 FLAG_RANGE_CHECK 决定。 0 使用覆盖值；-ENOENT 表示已知国家无该点差异，
 * 复用通用配置； -ENOTSUP 表示未知国家。回调必须只读、确定、非阻塞，不得重入 DP。 数组初值指针指向生命周期覆盖配置使用期的
 * const 数据。
 */
typedef int (*dp_country_parameter_fn)(uint16_t country, uint16_t dev, uint16_t sig,
                                      DM_SIG_DATA_S *value,
                                      bool *restore);

/**
 * @brief DP 初始化之前固定国家选择；默认 COMMON，无运行期切换。
 *
 * 非 COMMON 必须提供覆盖回调，不能与旧的默认/范围回调混用。 初始化成功后返回 -EBUSY；不会修改当前值、保存快照或执行恢复默认。
 */
int dp_register_parameter_country(uint16_t country, dp_country_parameter_fn override);

/**
 * @brief 查询设备挂载的处理表配置，不读取当前值、不修改 NVS 或 RAM 动态覆盖。
 *
 * 返回的数组指针只读；无数组初值返回全零配置。 未知国家 -ENOTSUP，未知设备/信号 -ENOENT；失败不修改输出。
 */
int dp_point_parameters_get(uint16_t country, uint16_t dev, uint16_t sig,
                             DM_SIG_DATA_S *value, bool *restore);

#endif
