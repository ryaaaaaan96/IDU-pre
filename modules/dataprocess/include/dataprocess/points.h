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

typedef enum { DP_U16, DP_S16, DP_U32, DP_S32, DP_WORDS, DP_U8 } dp_value_type_t;

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

typedef enum {
    DP_SOURCE_CONSTANT,
    DP_SOURCE_LOCAL,
    DP_SOURCE_PARAMETER,
} dp_source_t;

struct dp_point_cfg {
    struct dp_key key;
    dp_source_t source;
    dp_value_type_t type;
    uint8_t width;
    uint8_t size_bytes;
    uint8_t flags;
    dp_write_fn handler;
    dp_read_fn read;
    struct dp_value initial;
    int64_t minimum;
    int64_t maximum;
};

struct dp_model_cfg {
    uint16_t device_id;
    const DM_SIG_CFG_S *points;
    size_t point_count;
};

/** @brief 跳过参数区挂载，仅用于无 Flash 的测试。 */
#define DP_NO_PARAMS 0xFF

/**
 * @brief 登记一组模型；业务层逐组调用，顺序不限。
 *
 * 每组用不同 device_id 区分，可复用相同 sig_id；点值字数和持久参数数量按全部
 * 已登记模型累计校验。params_area 由首次调用确定，后续调用须传同一值。
 * 参数区在第一次访问参数接口时挂载；失败只记录状态并继续使用 RAM 默认值。
 * 登记本组后仍可登记其他组，但不能在参数区挂载后再加入持久参数（-EBUSY）。
 */
int dp_register_model(const struct dp_model_cfg *model, uint8_t params_area);

/**
 * @brief 同步写入单个点，字节长度必须等于配置长度。
 *
 * 常量点返回 -EACCES；命令点只执行回调，不保存触发值；参数点同步提交 NVS。
 */
int dp_set_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t size, uint8_t *bytes);

/** @brief 直接读取，不运行写回调；成功时返回传入容量。 */
int dp_get_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *bytes);
int dp_read(struct dp_key key, struct dp_point_state *state);

void dp_stop(void);
bool dp_stopped(void);

#endif
