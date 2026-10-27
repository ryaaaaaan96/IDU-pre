/**
 * @file
 * @brief 参考平台配置和业务回调登记接口。
 */
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

/**
 * @brief 在 dp_init 前登记；回调可选。
 *
 * 范围、加载和迁移回调也可能在查询配置时执行，必须确定、非阻塞， 不得修改配置或重入 DP 接口。变更和日志回调由 DP 任务异步调用。
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

/**
 * @brief 在 dp_init() 前登记，成功初始化后冻结登记。
 *
 * 设备表和旧版默认值、范围回调保留原契约。 新产品直接将数据写入 DM_SIG_CFG_S， 通过 sig_flag 中的
 * FLAG_RANGE_CHECK / FLAG_NO_RESTORE 控制范围检查和默认值恢复。 默认值、范围回调覆盖行数据，
 * 与国家覆盖配置互斥。未提供回调时， pltcb_para_get_range_def 读取所选行的数据并报告
 * recover_default=false， 不会强制用默认值覆盖 NVS。 CONFIG_DP_REFERENCE_TABLES
 * 向引擎提供受支持的常量表。
 */
/**
 * @brief 初始化前直接登记设备表与数量；不复制数组。
 * @note 表及其引用须常驻，登记后不得修改。与设备表回调和原生配置提供者互斥。
 * @return 0 成功，-EINVAL 参数无效，-EWOULDBLOCK ISR 调用，-EBUSY 配置已登记或冻结。
 */
int dp_register_devices(const DEV_TABLE_S *devices, uint16_t count);
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

/**
 * @brief 参数恢复后的显式 Zephyr 启动阶段。
 *
 * 校验所有全局 INIT/TIMER 登记项，按表顺序执行 INIT， 再启用定时器。回调接收四个 NULL 参数， 使用 dp_*
 * 事务辅助接口。仅支持 type=0 周期定时器， 其他类型被拒绝。定时工作只向 DP 业务任务提交已复制请求。 成功初始化仅允许一次；
 * INIT 失败时不回滚此前已成功的副作用。
 */
int dp_reference_init(void);

struct dp_equip_cfg;
/**
 * @brief 为已使用原生表的产品提供显式 Zephyr 配置扩展， 包含持久化与迁移钩子。
 *
 * 与原始 DEV_TABLE_S 提供器互斥， 不强转表指针，也不增加 ARC 包装层。
 */
typedef const struct dp_equip_cfg *(*dp_config_provider_fn)(void);
int dp_register_config_provider(dp_config_provider_fn provider);

/**
 * @brief UPP 模型的 rqst_cyl 保留编码后的调度策略。
 *
 * Modbus 表直接配置 period_ms，无需登记换算函数。
 */
typedef int (*dp_model_period_fn)(uint8_t encoded, uint32_t *period_ms);
int dp_register_model_period(dp_model_period_fn convert);
/**
 * @brief 启动任务前校验并提供不可变参考表视图。
 *
 * 返回配置归 DP 所有， 复位前有效；不分配测点、采集或转发描述符副本。须先登记提供器。
 */
int dp_reference_prepare(const struct dp_equip_cfg **cfg);

#endif
