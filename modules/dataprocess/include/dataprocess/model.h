/**
 * @file
 * @brief 数据模型配置；不包含采集或转发配置。
 */
#ifndef DP_MODEL_H
#define DP_MODEL_H
#include <dataprocess/reference_tables.h>
#include <stddef.h>

struct dp_model_cfg {
    uint16_t device_id;
    const DM_SIG_CFG_S *points;
    size_t point_count;
};

/**
 * @brief 初始化前一次登记多个数据模型，不复制配置。
 * @note 数组及测点定义须常驻且登记后不可修改；随后调用 dp_init()。
 * @return 0 成功；-EINVAL 配置无效；-EEXIST 设备重复；-EBUSY 已登记或初始化；
 *         -EWOULDBLOCK ISR 调用；-ENOTSUP 模型含 FLAG_COLLECT。
 */
int dp_register_models(const struct dp_model_cfg *models, size_t count);
#endif
