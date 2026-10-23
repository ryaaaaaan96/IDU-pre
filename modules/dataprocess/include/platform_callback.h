#ifndef DP_PLATFORM_CALLBACK_H
#define DP_PLATFORM_CALLBACK_H

#include <dataprocess/reference_tables.h>

typedef const DEV_TABLE_S *(*CFG_GET_DEV_TBL)(void);
typedef uint16_t (*CFG_GET_DEV_NUM)(void);
typedef const TIME_DATA_SET_S *(*CFG_GET_TIMER_REG_TBL)(void);
typedef uint16_t (*CFG_GET_TIMER_REG_NUM)(void);
typedef const INIT_DATA_SET_S *(*CFG_GET_INIT_REG_TBL)(void);
typedef uint16_t (*CFG_GET_INIT_REG_NUM)(void);
typedef int (*PARA_GET_RANGE_DEF)(uint16_t dev_id, uint16_t sig_id,
                                 DM_SIG_DATA_S *value, bool *recover_default);
typedef bool (*PARA_GET_IS_LOAD_DEF)(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);
typedef bool (*PARA_GET_IS_RANGE_CHECK)(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);
typedef struct {
    const uint16_t dev_id, sig_id;
    const uint8_t sig_store_size, sig_cfg_size;
    const uint8_t *p_store_value;
    uint8_t *p_load_value;
    const uint32_t cfg_data_type;
} DM_SIG_CFG_DATA_S;
typedef bool (*PARA_SIZE_MISMATCH_PRO)(DM_SIG_CFG_DATA_S *data);
typedef struct {
    uint32_t date_time;
    uint16_t port_id, dev_id, sig_id, sig_size, reg_addr;
    DATA_U old_value, new_value;
    UPP_REG_ADDR upp_addr;
    FUNC_PARA_REG_TYPE addr_type;
} DP_PARA_LOG_DATA;
typedef void (*PARA_WRITE_LOG)(DP_PARA_LOG_DATA *data);
typedef void (*PARA_RECOVER_FINISH)(void);

/* Register before dp_init. Callbacks are optional. Range/load/migration
 * callbacks may also run during configuration lookup. They must be deterministic,
 * nonblocking, and must not change configuration or reenter DP APIs.
 * Change/log callbacks are delivered asynchronously by the DP worker.
 */
void pltcb_reg_para_is_load_def_cb(PARA_GET_IS_LOAD_DEF callback);
bool pltcb_para_get_is_load_def(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);
void pltcb_reg_para_is_range_check(PARA_GET_IS_RANGE_CHECK callback);
bool pltcb_para_get_is_range_check(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);
void pltcb_reg_para_size_mismatch_cb(PARA_SIZE_MISMATCH_PRO callback);
bool pltcb_para_size_mismatch_pro(DM_SIG_CFG_DATA_S *data);
void pltcb_reg_para_write_log_cb(PARA_WRITE_LOG callback);
void pltcb_para_write_log(DP_PARA_LOG_DATA *data);
void pltcb_reg_para_recover_finish_cb(PARA_RECOVER_FINISH callback);
void pltcb_para_recover_fininsh(void);

/* Register before dp_init(). Successful initialization freezes registration.
 * Device table and legacy default/range callbacks retain their contracts.
 * New products put data directly in DM_SIG_CFG_S, controlling range checks and
 * default restoration with FLAG_RANGE_CHECK / FLAG_NO_RESTORE in sig_flag. The
 * default/range callback overrides row data and is incompatible with country
 * overrides. Without it, pltcb_para_get_range_def reads the selected row data
 * and reports recover_default=false, never forcing defaults over NVS.
 * CONFIG_DP_REFERENCE_TABLES exposes supported const tables to the engine.
 */
void pltcb_reg_cfg_get_dev_tbl(CFG_GET_DEV_TBL callback);
const DEV_TABLE_S *pltcb_cfg_get_dev_tbl(void);
void pltcb_reg_cfg_get_dev_num(CFG_GET_DEV_NUM callback);
uint16_t pltcb_cfg_get_dev_num(void);
void pltcb_reg_para_range_def_cb(PARA_GET_RANGE_DEF callback);
int pltcb_para_get_range_def(uint16_t dev_id, uint16_t sig_id,
                           DM_SIG_DATA_S *value, bool *recover_default);
void pltcb_reg_cfg_get_timer_reg_tbl(CFG_GET_TIMER_REG_TBL callback);
const TIME_DATA_SET_S *pltcb_cfg_get_timer_reg_tbl(void);
void pltcb_reg_cfg_get_timer_reg_num(CFG_GET_TIMER_REG_NUM callback);
uint16_t pltcb_cfg_get_timer_reg_num(void);
void pltcb_reg_cfg_get_init_reg_tbl(CFG_GET_INIT_REG_TBL callback);
const INIT_DATA_SET_S *pltcb_cfg_get_init_reg_tbl(void);
void pltcb_reg_cfg_get_init_reg_num(CFG_GET_INIT_REG_NUM callback);
uint16_t pltcb_cfg_get_init_reg_num(void);

/* Explicit Zephyr startup stage after parameter recovery. Validate all global
 * INIT/TIMER registrations, execute INIT in table order, then enable timers.
 * Callbacks receive four NULL arguments and use dp_* transaction helpers.
 * type=0 periodic timers are supported; other types are rejected. Timer work
 * only submits copied requests to the DP business worker. Successful initialization is
 * one-time; after INIT failure, earlier successful effects are not rolled back.
 */
int dp_reference_init(void);

struct dp_equip_cfg;
/* Explicit Zephyr configuration extension for products already using native
 * tables (including persistence/migration hooks). Mutually exclusive with the
 * original DEV_TABLE_S provider; no table pointer casts or ARC wrapper.
 */
typedef const struct dp_equip_cfg *(*dp_config_provider_fn)(void);
int dp_register_config_provider(dp_config_provider_fn provider);

/* UPP model rqst_cyl retains its encoded scheduling policy. Modbus tables
 * configure period_ms directly and require no conversion registration.
 */
typedef int (*dp_model_period_fn)(uint8_t encoded, uint32_t *period_ms);
int dp_register_model_period(dp_model_period_fn convert);
/* Validate and expose immutable reference table views before starting workers.
 * The returned configuration belongs to DP and is valid until reset. No copies
 * of point/collect/transmit descriptors are allocated. Register providers first.
 */
int dp_reference_prepare(const struct dp_equip_cfg **cfg);

#endif
