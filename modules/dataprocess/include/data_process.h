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
typedef struct func_para_all {
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
typedef void (*DM_PF_ATTR_SIG_CHG_PROC)(uint16_t dev, uint16_t sig, uint8_t size,
                                       uint32_t type, const uint8_t *value);
typedef enum { PARA_VALUE_REG_OLD, PARA_VALUE_REG_NEW } PARA_VALUE_REG_TYPE;
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
/* Callback-only one-shot action for a single volatile WRITE point. Executed
 * synchronously after all callbacks and value checks succeed, instead of
 * storing the trigger value. No INIT/READ/SAMPLE, derived outputs, parameter
 * writes or finish actions may share this request. No automatic replay.
 * The action returns 0 or -errno; it must not reenter the process engine.
 */
int dp_defer_para_action(int (*action)(uint16_t argument), uint16_t argument);
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

typedef struct dev_table_s DEV_TABLE_S;
typedef struct dev_tbl_cfg_s DEV_TBL_CFG_S;
typedef struct dm_sig_data_s DM_SIG_DATA_S;
#include <dataprocess/reference_tables.h>

/* Configuration views are read-only and remain valid for the configuration's
 * lifetime. Native catalogs without original protocol tables return NULL for
 * those tables, never reinterpret a native layout as a reference layout.
 */
int32_t dp_dev_get_num(void);
const DEV_TABLE_S *dp_dev_get_tbl(void);
const DEV_TABLE_S *dp_dev_get_from_devid(uint16_t dev_id);
int32_t dp_dev_get_from_portid(uint16_t port_id, DEV_TABLE_S *device);
int32_t dp_dev_get_coll_addr(uint8_t dev_id);
uint8_t dp_dev_get_port(uint8_t dev_id);
uint16_t dp_dev_get_code(uint8_t dev_id);
const DEV_TBL_CFG_S *dp_devid_get_prototbl(uint8_t dev_id, uint16_t protocol);
const void *dp_sigcfg_get_by_devcfg(const DEV_TBL_CFG_S *tables, uint16_t protocol,
                                   uint16_t *count);
const void *dp_sigcfg_get_by_devid(uint16_t dev_id, uint16_t protocol, uint16_t *count);
/* For values over 4 bytes, getter callers supply def_value.pst with room for
 * the configured byte length. Setter copies it into a bounded sparse override;
 * const configuration and current values are unchanged. No NVS persistence.
 */
int32_t dp_get_para_def_range(uint16_t dev_id, uint16_t sig_id, DM_SIG_DATA_S *data);
int32_t dp_set_para_def_range(uint16_t dev_id, uint16_t sig_id, DM_SIG_DATA_S *data);
/* Run matching registered INIT functions, then the device's point handlers.
 * Earlier successful initializers are not rolled back on a later failure.
 */
int32_t dp_set_init(uint16_t dev_id);
int32_t dp_dm_reset_dev_data(uint8_t dev_id);
/* Up to three observers; copied changes run on the DP jobs thread. The bounded
 * event queue drops notifications on overflow without undoing committed data.
 */
int32_t dp_reg_sig_callback(DM_PF_ATTR_SIG_CHG_PROC callback);
int32_t dp_para_value_reg(uint16_t type, DATA_SET_S *data);
/* Reference direct-collector path, separate from durable native transports.
 * Register once. Backend returns 0 when accepted and completes exactly once;
 * an error means rejection with no later completion. Requests are copied.
 * NO_DM waits up to 1 s; timeout holds storage until a late completion arrives.
 * Direct device writes cannot be rolled back by a later transaction failure.
 */
int32_t dp_para_reg_set_func(PARA_SET_FUNC callback);
int32_t dp_set_para_not_dm(PARA_DATA_S *data, PARA_SET_STATUS status);
/* Callback-only flags, applied after successful transaction commit. */
void dp_flush_para_store(void);
void dp_set_reboot(void);
/* Requires DP_PARAMS; flushes and schedules reboot after 100 ms. */
void dp_reboot_system(void);
/* Only RESTORE parameters. Both honor the default policy; factory additionally
 * flushes durable storage, without erasing partitions or implicitly rebooting.
 * Void legacy APIs report failures through logging; native APIs return errno.
 */
void dp_load_def_data(void);
void dp_reset_factory(void);
#endif
