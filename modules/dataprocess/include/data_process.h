#ifndef DATA_PROCESS_H
#define DATA_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Source-level reference interfaces, using standard C and Zephyr directly.
 * Byte buffers use the reference ARM little-endian representation. They may be
 * unaligned. This is not a binary ABI for the proprietary platform library.
 */
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

typedef enum { FUNC_PARA_REG_TYPE_MB, FUNC_PARA_REG_TYPE_UPP } FUNC_PARA_REG_TYPE;
typedef struct { uint16_t model, offset; } UPP_REG_ADDR;
typedef struct {
    FUNC_PARA_REG_TYPE addr_type;
    union { uint16_t reg_addr; UPP_REG_ADDR upp_addr; };
} FUNC_PARA_RD;
typedef struct {
    uint16_t dev_id, sig_id, sig_size;
    FUNC_PARA_RD extra;
    uint8_t *pvalue;
} FUNC_PARA_S;
typedef struct {
    uint8_t type;
    uint16_t port_id, para_num;
    FUNC_PARA_S *para_all;
} FUNC_PARA_ALL;
typedef int32_t (*DM_PF_SIG_PROC)(FUNC_PARA_ALL *inpara, FUNC_PARA_ALL *outpara,
                                void *new_value, void *old_value);
typedef struct {
    uint16_t dev_id, sig_id, sig_size;
    FUNC_PARA_RD extra;
    DATA_U p_value;
    uint16_t data_type;
} DATA_SET_S;
typedef struct {
    uint16_t type, port_id, set_num, can_id;
    DATA_SET_S *p_set_data;
} PARA_DATA_S;
typedef void (*PARA_SET_STATUS)(PARA_DATA_S *data);
typedef int32_t (*PARA_SET_FUNC)(PARA_DATA_S *data, PARA_SET_STATUS status);
typedef enum { DP_PARA_SET, DP_PARA_GET, DP_PARA_INIT } DP_PARA_OPT;
typedef enum { DP_FAST_SET, DP_TIMER_SET, DP_NO_DM_SET } DP_CALLBACK_TYPE;
typedef enum {
    PARA_NO_SET_CO, PARA_NO_SET_DM, PARA_NO_STORE_DM
} DP_PARA_FLAG_BIT;
typedef enum {
    SIG_FLAG_PERSISTENCE, SIG_FLAG_COLLECT, SIG_FLAG_RT_STORE,
    SIG_FLAG_READ_CALLBACK, SIG_FLAG_PERSISTENCE_SYNC
} SIG_FLAG_BIT;
typedef enum {
    FLAG_PERSISTENCE = 1, FLAG_COLLECT = 2, FLAG_RT_STORE = 4,
    FLAG_READ_CALLBACK = 8, FLAG_PERSISTENCE_SYNC = 16
} SIG_FLAG_DEF;

#define DP_FUNC_PARA_CONST(para, dev, sig, size, value) do { \
    (para).dev_id = (dev); (para).sig_id = (sig); \
    (para).sig_size = (size); (para).pvalue = (uint8_t *)(value); \
} while (0)

int32_t dp_init(void);
/* Thread-only synchronous write: exact configured byte count; 0 or -errno.
 * Local, remote and writable parameter signals are accepted. Callbacks finish
 * before return. Per-device persistence retains the native recovery protocol.
 */
int32_t dp_set_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t size, uint8_t *value);
/* Direct store read, without READ callbacks. Copies the configured byte count;
 * on success returns supplied capacity, preserving the reference quirk.
 */
int32_t dp_get_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *value);
/* Values of <=4 bytes live in p_value.str; larger values use p_value.pst.
 * Fast get requires exactly one entry and its exact configured size, and runs
 * an enabled READ callback. A nonzero callback status is returned unchanged;
 * any nonzero status preserves response bytes but aborts derived outputs.
 * Native protocol reads translate a positive READ status to -EIO.
 * Fast set is synchronous; completion is called once
 * after a successful transaction. Only DP_FAST_SET requests are supported.
 */
int32_t dp_fast_para_set(PARA_DATA_S *data, PARA_SET_STATUS status);
int32_t dp_fast_para_get(PARA_DATA_S *data);
int32_t dp_get_sig_data_type(uint16_t dev_id, uint16_t sig_id,
                           uint16_t *data_type, uint16_t *size);
/* Transaction callback helpers. Values and flags apply to this transaction;
 * outputs are copied, validated and committed after callbacks succeed. These
 * helpers fail outside the current callback thread. No nested engine calls.
 */
int32_t dp_get_para_value(FUNC_PARA_S *para);
int32_t dp_get_para_old_value(FUNC_PARA_S *para);
int32_t dp_check_para_set(FUNC_PARA_S *para);
int32_t dp_set_para_value(FUNC_PARA_S *para);
int32_t dp_set_para_value_flag(FUNC_PARA_S *para, uint8_t flags);
int32_t dp_set_para_flag(uint16_t dev_id, uint16_t sig_id, uint8_t flags);
void dp_set_para_no_collect(uint16_t dev_id, uint16_t sig_id);
int32_t dp_set_para_only_dm(uint16_t dev_id, uint16_t sig_id,
                          uint8_t size, uint8_t *value);
int32_t dp_set_para_only_dm_no_store(uint16_t dev_id, uint16_t sig_id,
                                   uint8_t size, uint8_t *value);

struct dp_process_ctx;
struct dp_key;
/* Global registered INIT/TIMER callbacks run with all four arguments NULL and
 * can emit outputs through the scoped helpers. Call from a Zephyr thread.
 */
int dp_reference_invoke(DM_PF_SIG_PROC callback, bool timer);
/* Nonblocking enqueue; executes on the DP business worker, not system workq. */
int dp_reference_submit(DM_PF_SIG_PROC callback, bool timer);
int dp_reference_process(struct dp_process_ctx *ctx, struct dp_key key);

#include <dataprocess/reference_tables.h>
#endif
