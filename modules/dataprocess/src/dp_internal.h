/**
 * @file
 * @brief DP 模块内部共享的最小接口。
 */
#ifndef DP_INTERNAL_H
#define DP_INTERNAL_H

#include <dataprocess/points.h>

typedef enum { DP_U16, DP_S16, DP_U32, DP_S32, DP_WORDS, DP_U8 } dp_value_type_t;

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

int dp_params_configure(uint8_t area_id);
bool dp_params_configured(void);
int dp_params_read(struct dp_key key, struct dp_point_state *state);
bool dp_params_mounted(void);
int dp_params_write(struct dp_key key, const struct dp_value *value);
size_t dp_point_count(void);
int dp_config_index(struct dp_key key);
const struct dp_point_cfg *dp_config_point(size_t index, struct dp_point_cfg *scratch);
int dp_check_value(const struct dp_point_cfg *point, const struct dp_value *value);

#endif
