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

/* Register before dp_init(). Successful initialization freezes registration.
 * Original device table and default/range callbacks retain their contracts.
 * CONFIG_DP_REFERENCE_TABLES compiles supported tables into the Zephyr engine.
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
 * only submits copied requests to the DP business worker. Successful start is
 * one-time; after INIT failure, earlier successful effects are not rolled back.
 */
int dp_reference_start(void);

struct dp_equip_cfg;
/* Explicit Zephyr configuration extension for products already using native
 * tables (including persistence/migration hooks). Mutually exclusive with the
 * original DEV_TABLE_S provider; no table pointer casts or ARC wrapper.
 */
typedef const struct dp_equip_cfg *(*dp_config_provider_fn)(void);
int dp_register_config_provider(dp_config_provider_fn provider);

/* Original rqst_cyl is an encoded scheduling policy. A product must supply its
 * conversion before importing collection tables. Zero milliseconds denotes an
 * initialization-only request. The compiler never guesses a time unit.
 */
typedef int (*dp_collect_period_fn)(uint8_t encoded, uint32_t *period_ms);
int dp_register_collect_period(dp_collect_period_fn convert);

#endif
