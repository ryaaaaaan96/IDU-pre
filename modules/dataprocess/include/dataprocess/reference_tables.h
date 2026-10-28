/**
 * @file
 * @brief 产品测点行的最小描述。
 *
 * 保留参考平台的字段命名，供应用点表直接使用；DP 不再提供参考目录、
 * 字节适配或协议表实现。
 */
#ifndef DP_REFERENCE_TABLES_H
#define DP_REFERENCE_TABLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 参考接口的数据类型编码；当前产品只使用其中的整数和数组类型。 */
typedef enum {
    DATA_TYPE_NULL = 0, DATA_TYPE_S8, DATA_TYPE_U8, DATA_TYPE_S16,
    DATA_TYPE_U16, DATA_TYPE_S32, DATA_TYPE_U32, DATA_TYPE_S64, DATA_TYPE_U64,
    DATA_TYPE_FLOAT, DATA_TYPE_DOUBLE, DATA_TYPE_BOOL, DATA_TYPE_STRING,
    DATA_TYPE_BITSTRING, DATA_TYPE_DTAETIME, DATA_TYPE_SIMPLETIME,
    DATA_TYPE_STATECHANGE, DATA_TYPE_DEVICEFILE, DATA_TYPE_U8ARRAY,
    DATA_TYPE_U16ARRAY, DATA_TYPE_U16CONTINUE = 101
} DATA_TYPE_E;

typedef union data_u {
    uint16_t u16;
    int16_t s16;
    uint32_t u32;
    int32_t s32;
    float f32;
    void *pst;
    uint8_t str[4];
} DATA_U;

typedef struct dm_sig_data_s {
    DATA_U def_value;
    DATA_U min_value;
    DATA_U max_value;
} DM_SIG_DATA_S;

#define DP_POINT_DEFAULT_VALUE 0
#define DP_POINT_DEFAULT_FLAGS 0U
#define DP_POINT_DEFAULT_CALLBACK NULL
#define DP_POINT_DEFAULT_ARRAY NULL

typedef enum {
    FLAG_PERSISTENCE = 1,
    FLAG_READ_CALLBACK = 8,
    FLAG_NO_RESTORE = 32, FLAG_RANGE_CHECK = 64, FLAG_CONSTANT = 128
} SIG_FLAG_DEF;

struct dp_key {
    uint16_t dev_id;
    uint16_t sig_id;
};

struct dp_value;
struct dp_point_state;
typedef int (*dp_write_fn)(struct dp_key key, const struct dp_value *value);
typedef int (*dp_read_fn)(struct dp_key key, struct dp_point_state *state);

typedef struct dm_sig_cfg_s {
    uint16_t sig_id;
    uint8_t sig_size;
    uint8_t sig_flag;
    uint32_t data_type;
    dp_write_fn handler;
    dp_read_fn read;
    DM_SIG_DATA_S data;
} DM_SIG_CFG_S;

#endif
