/**
 * @file
 * @brief 参考平台兼容的采集接口。
 */
#ifndef COLLECT_LIB_H
#define COLLECT_LIB_H

#include <stdint.h>
#include <collect/collect.h>
#include <dataprocess/reference_tables.h>

typedef enum { SET_PRI_L1 = 1, SET_PRI_L2 = 2, SET_PRI_L3 = 3 } CO_SET_PRI;
typedef enum {
    CO_DEV_UNKNOWN = 0, CO_DEV_OFFLINE = 1, CO_DEV_ONLINE = 2, CO_DEV_EXCEPTION = 3,
    CO_DEV_INITIALIZING = 4,
} CO_DEV_ONLIE;
typedef enum { SCAN_PRI_LOW = 0, SCAN_PRI_HIGH = 1 } CO_SCAN_PRI;
/**
 * @brief 点表元数据取值；声明这些值不代表支持 FC17。
 */
typedef enum {
    FLAG_FRAME = 1, FLAG_0x03_W_0x06 = 2, FLAG_0x03_w_0x10 = 4, FLAG_0x17_W_CODE = 8,
    FLAG_INITIALIZE = CO_TABLE_INIT,
} CO_TABLE_FLAG;

/**
 * @brief 采集表可选字段的编译期默认值；周期和读写目标由应用明确配置。
 */
#define CO_TABLE_DEFAULT_FLAGS 0U
#define CO_TABLE_DEFAULT_PRIORITY 0U
#define CO_TABLE_DEFAULT_DATA_TYPE 0U
#define CO_TABLE_DEFAULT_BYTE_ORDER BYTE_ORDER_DEFAULT
#define CO_TABLE_DEFAULT_SCAN_PRIORITY SCAN_PRI_LOW

/**
 * @brief 参考平台数值；MODE_OTHER 不受支持，也不等同于 CO_PORT_SLEEP。
 */
typedef enum {
    MODE_IDLE = 0, MODE_INIT = 1, MODE_UPDATE = 2, MODE_IV = 3,
    MODE_FAULT = 4, MODE_TRAN = 5, MODE_OTHER = 6,
} CO_PORT_MODE;
typedef enum { CO_SCAN_PKG_ON = 0, CO_SCAN_PKG_OFF = 1 } CO_SCAN_PKG_FLAG;
typedef struct { uint16_t port_id; } CO_CLEAN_QUEUE_ST;
typedef int32_t (*CO_PORT_PF_PROC)(uint16_t port_id);
typedef struct {
    uint16_t dev_id, sig_id, sig_size;
    uint8_t trans_val[64];
} CO_SET_VALUE_TRANS_S;
typedef int32_t (*CO_SET_VALUE_TRANS_CB)(CO_SET_VALUE_TRANS_S *data);

/**
 * @brief port_id 对应配置的 co_bus_cfg.id。
 *
 * 返回 0 或 -errno， 不返回 ARC 状态码。仅在线程中调用；timeout_ms 限制获取锁的等待时间， 设备正在初始化时仍返回忙；0 表示不等待。
 */
int co_set_port_mode(uint16_t port_id, uint8_t mode, uint32_t timeout_ms);
int co_get_port_mode(uint16_t port_id, uint8_t *mode);
int co_dev_init(uint16_t dev_id);
int co_init_func_reg(uint16_t port_id, CO_PORT_PF_PROC callback);
int co_set_value_trans_cb_reg(CO_SET_VALUE_TRANS_CB callback);
/**
 * @brief 参考接口的同步底层写入。
 *
 * 发送前复制并校验数值；后续失败时， 前面的设备写入可能已成功。全部成功后仅调用一次完成回调， 不修改 DM 或 NVS。
 * 使用显式参数转发地址或 FC3 采集映射， 不推断 FC4 输入寄存器可写。换算仅用于此参考直接写入路径， 不用于原生持久化传输。
 */
int co_dp_fast_set(PARA_DATA_S *data, PARA_SET_STATUS status);
/**
 * @brief 完整 RTU 响应，包含站号、功能码、字节数和 CRC。
 *
 * reg_addr 使用配置的地址约定，不隐式加减 1。 所有映射字段必须能放入一个有界 DP 批次，否则不发布任何数据。
 */
int co_anlysis_mb_pkg(uint8_t dev_id, uint16_t reg_addr, uint8_t *frame, uint16_t length);
void co_set_reboot_system(void);
/**
 * @brief 排队请求仅以 -ECANCELED 取消一次，正在执行的请求允许完成。
 */
int co_clean_set_queue(CO_CLEAN_QUEUE_ST *queue);
/**
 * @brief RTU 按地址和功能码匹配，模型后端按模型和偏移匹配； 所有匹配块一起更新。
 *
 * 同一设备混用协议会产生歧义，应拒绝。 ON/OFF 不影响显式 co_poll() 或仅初始化读取。
 */
int co_set_scan_pkg_flag(uint8_t dev_id, uint16_t reg_addr_model,
                         uint16_t reg_type_offset, uint8_t flag);

#endif
