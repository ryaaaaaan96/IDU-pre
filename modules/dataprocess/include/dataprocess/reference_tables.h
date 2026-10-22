#ifndef DP_REFERENCE_TABLES_H
#define DP_REFERENCE_TABLES_H

#include <data_process.h>

/* Table vocabulary from the reference platform, extended for Zephyr products.
 * They describe business data, not an ARC/RTOS abstraction or binary ABI.
 */
#define MAX_PTL_TBL_NUM 7

typedef enum {
    PRO_DEFAULT = 0, PRO_DATA_PROCESS = 1, PRO_MB_MASTER = 2,
    PRO_MB_SLAVE = 3, PRO_IEC104_SLAVE = 4, PRO_IEC61850_MMS_SRV = 5,
    PRO_MQTT_SLAVE = 6, PRO_FUPIN_SLAVE = 7, PRO_IEC104_MASTER = 8,
    PRO_FTP_SLAVE = 9, PRO_EGD_SLAVE = 10, PRO_IEC61850_GSE_SRV = 11,
    PRO_WEB_XML = 12, PRO_GSE_MASTER = 13, PRO_INCRE_SLAVE = 14,
    PRO_ADDR_SLAVE = 15, PRO_UPP_MASTER = 16, PRO_UPP_SLAVE = 17,
    PRO_UPP_SLAVE_INCRE = 18, PRO_UPP_MASTER_INCRE = 19, PRO_MAX_NUM
} PTL_TYPE_E;

typedef struct dm_sig_data_s {
    DATA_U def_value;
    DATA_U min_value;
    DATA_U max_value;
} DM_SIG_DATA_S;

/* DP 可选字段的编译期默认值。应用逐字段填写，不用整行初始化宏。
 * 数组默认指针由 DP 按 sig_size 解释为全零；零初值不表示已有有效采样。
 * 信号 ID、类型、长度及业务范围仍须由应用明确配置。
 */
#define DP_POINT_DEFAULT_VALUE 0
#define DP_POINT_DEFAULT_FLAGS 0U
#define DP_POINT_DEFAULT_CALLBACK NULL
#define DP_POINT_DEFAULT_ARRAY NULL

typedef struct dm_sig_cfg_s {
    uint16_t sig_id;
    uint8_t sig_size;
    uint8_t sig_flag;
    uint32_t data_type;
    DM_PF_SIG_PROC pf_sig_proc;
    /* 默认值和范围常驻 Flash；FLAG_RANGE_CHECK 开启配置上下限检查。
     * 未开启时使用类型范围；开启且双零时只允许零。仅支持标量整数。
     * 大于四字节时 def_value.pst 指向静态 const 数据，NULL 表示全零。
     * FLAG_NO_RESTORE 禁止主动恢复默认，不影响启动加载 NVS。
     */
    DM_SIG_DATA_S data;
} DM_SIG_CFG_S;

typedef struct dev_tbl_cfg_s {
    uint16_t proto_type;
    uint16_t sig_num;
    const void *pst_table;
} DEV_TBL_CFG_S;

typedef struct dev_table_s {
    uint8_t dev_id;
    uint8_t port;
    uint8_t coll_addr;
    uint16_t dev_code;
    DEV_TBL_CFG_S ptl_tbl_cfg[MAX_PTL_TBL_NUM];
} DEV_TABLE_S;

typedef struct times_set_s {
    uint8_t type;
    uint32_t timing;
    DM_PF_SIG_PROC function;
} TIME_DATA_SET_S;

typedef struct init_set_s {
    uint16_t dev_id;
    DM_PF_SIG_PROC function;
} INIT_DATA_SET_S;

typedef struct {
    uint16_t sig_id;
    uint16_t reg_addr;
    uint8_t reg_num;
    uint8_t reg_type;
    /* 毫秒范围 0–65535；0：只读点初始化采集，FLAG_COLLECT 点仅下发。 */
    uint16_t period_ms;
    uint8_t flag;
    uint8_t priority : 2;
    uint8_t data_type : 3;
    uint8_t byte_order : 3;
    uint8_t scan_pri : 1;
} MODBUS_RTU_TABLE_S;

typedef struct mb_table_cell_t {
    uint16_t sig_id;
    uint16_t reg_addr;
    uint8_t reg_num;
    uint8_t dev_id;
    uint8_t reg_type;
    uint8_t byte_order;
} MB_TABLE_CELL_T, MB_TABLE_CELL_S;

/* UPP model-table subset from upp_m_ex.h. The backend exchanges decoded
 * host-order words; this declares model data, not the UPP serial protocol.
 */
typedef struct upp_m_model_data {
    uint16_t mod_data_offset;
    uint16_t mod_data_len;
    uint16_t mod_data_type;
    uint16_t data_len;
    uint32_t sig_id;
    uint16_t data_type;
    uint8_t rw_mode : 3;
    uint8_t byte_order : 2;
    uint8_t set_pri : 2;
    uint8_t scan_pri : 1;
    uint8_t rqst_cyl;
    double dec_p;
} UPP_M_MOD_DATA;

typedef struct upp_m_model {
    const UPP_M_MOD_DATA *p_data;
    uint16_t data_num;
    uint16_t mod_id;
} UPP_M_MOD;

typedef struct upp_m_model_reg {
    const UPP_M_MOD *p_model_ary;
    uint32_t model_num;
    uint32_t cfg_mode;
} UPP_M_MOD_REG;

/* Numeric encodings are protocol metadata; no arc_byte_order.h is needed. */
#define BYTE_ORDER_DEFAULT 0
#define BYTE_ORDER_SMALL 1
#define BYTE_ORDER_BIGSMALL 2
#define BYTE_ORDER_SMALLBIG 3
#define BYTE_ORDER_BIG 4

struct dp_transmit_cfg;
/* Decode one immutable reference row into a caller-owned execution descriptor. */
int dp_transmit_from_reference(const MB_TABLE_CELL_T *row, struct dp_transmit_cfg *map);
#endif
