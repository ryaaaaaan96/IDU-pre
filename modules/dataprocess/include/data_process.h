#ifndef DATA_PROCESS_H
#define DATA_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 参考平台的源码级接口，直接使用标准 C 和 Zephyr。
 * 字节缓冲区采用参考 ARM 小端表示，允许未对齐；
 * 不提供专有平台库的二进制 ABI。
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
    SIG_FLAG_READ_CALLBACK, SIG_FLAG_PERSISTENCE_SYNC,
    SIG_FLAG_NO_RESTORE, SIG_FLAG_RANGE_CHECK
} SIG_FLAG_BIT;
typedef enum {
    FLAG_PERSISTENCE = 1, FLAG_COLLECT = 2, FLAG_RT_STORE = 4,
    FLAG_READ_CALLBACK = 8, FLAG_PERSISTENCE_SYNC = 16,
    FLAG_NO_RESTORE = 32, FLAG_RANGE_CHECK = 64
} SIG_FLAG_DEF;

#define DP_FUNC_PARA_CONST(para, dev, sig, size, value) do { \
    (para).dev_id = (dev); (para).sig_id = (sig); \
    (para).sig_size = (size); (para).pvalue = (uint8_t *)(value); \
} while (0)

int32_t dp_init(void);
/* 仅在线程中同步写入，长度必须等于配置字节数，返回 0 或 -errno。
 * 接受本地、远端及可写参数信号，回调在返回前完成。
 * 按设备持久化仍使用原生恢复协议。
 */
int32_t dp_set_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t size, uint8_t *value);
/* 直接读取存储，不运行 READ 回调；复制配置的字节数。
 * 成功时返回传入容量，以保留参考接口的特殊行为。
 */
int32_t dp_get_sig_value(uint16_t dev_id, uint16_t sig_id, uint8_t capacity, uint8_t *value);
/* 不超过 4 字节的值放在 p_value.str，
 * 较大值使用 p_value.pst。
 * 快速读取要求恰好一个条目且长度与配置一致，
 * 并运行已启用的 READ 回调。回调非零状态原样返回：保留响应字节，
 * 但取消派生输出。原生协议读取将正 READ 状态转换为 -EIO。
 * 快速设置同步执行，事务成功后仅调用一次完成回调；
 * 仅支持 DP_FAST_SET 请求。
 */
int32_t dp_fast_para_set(PARA_DATA_S *data, PARA_SET_STATUS status);
int32_t dp_fast_para_get(PARA_DATA_S *data);
int32_t dp_get_sig_data_type(uint16_t dev_id, uint16_t sig_id,
                           uint16_t *data_type, uint16_t *size);
/* 事务回调辅助接口；数值和标志只作用于当前事务。回调成功后，
 * 输出才被复制、校验并提交。在当前回调线程之外调用会失败，
 * 禁止嵌套调用处理引擎。
 */
int32_t dp_get_para_value(FUNC_PARA_S *para);
int32_t dp_get_para_old_value(FUNC_PARA_S *para);
int32_t dp_check_para_set(FUNC_PARA_S *para);
int32_t dp_set_para_value(FUNC_PARA_S *para);
int32_t dp_set_para_value_flag(FUNC_PARA_S *para, uint8_t flags);
int32_t dp_set_para_flag(uint16_t dev_id, uint16_t sig_id, uint8_t flags);
/* 仅在回调中使用，为单个非持久 WRITE 点登记一次性动作。
 * 所有回调及数值检查成功后同步执行动作，不存储触发值。
 * 同一请求不得混入 INIT/READ/SAMPLE、派生输出、参数写入或完成动作，
 * 也不自动重放。动作返回 0 或 -errno，不得重入处理引擎。
 */
int dp_defer_para_action(int (*action)(uint16_t argument), uint16_t argument);
void dp_set_para_no_collect(uint16_t dev_id, uint16_t sig_id);
int32_t dp_set_para_only_dm(uint16_t dev_id, uint16_t sig_id,
                          uint8_t size, uint8_t *value);
int32_t dp_set_para_only_dm_no_store(uint16_t dev_id, uint16_t sig_id,
                                   uint8_t size, uint8_t *value);

struct dp_process_ctx;
struct dp_key;
/* 全局登记的 INIT/TIMER 回调以四个 NULL 参数执行，
 * 可通过当前作用域的辅助接口产生输出。须从 Zephyr 线程调用。
 */
int dp_reference_invoke(DM_PF_SIG_PROC callback, bool timer);
/* 非阻塞入队，在 DP 业务任务上执行，不使用系统工作队列。 */
int dp_reference_submit(DM_PF_SIG_PROC callback, bool timer);
int dp_reference_process(struct dp_process_ctx *ctx, struct dp_key key);

typedef struct dev_table_s DEV_TABLE_S;
typedef struct dev_tbl_cfg_s DEV_TBL_CFG_S;
typedef struct dm_sig_data_s DM_SIG_DATA_S;
#include <dataprocess/reference_tables.h>

/* 配置视图只读，在配置整个生命周期内有效。原生目录不包含原始协议表时，
 * 对应表返回 NULL，不得把原生布局强转为参考布局。
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
/* 超过 4 字节的值，
 * 读取方须在 def_value.pst 提供足够容纳配置长度的空间。
 * 设置时复制到有界稀疏覆盖表；不修改常量配置和当前值，也不写入 NVS。
 */
int32_t dp_get_para_def_range(uint16_t dev_id, uint16_t sig_id, DM_SIG_DATA_S *data);
int32_t dp_set_para_def_range(uint16_t dev_id, uint16_t sig_id, DM_SIG_DATA_S *data);
/* 先运行匹配的已登记 INIT 函数，再运行设备测点处理函数。
 * 后续初始化失败时，不回滚此前已成功的初始化。
 */
int32_t dp_set_init(uint16_t dev_id);
int32_t dp_dm_reset_dev_data(uint8_t dev_id);
/* 最多三个观察者；复制后的变更在 DP 作业线程执行。
 * 有界事件队列满时丢弃通知，不撤销已提交数据。
 */
int32_t dp_reg_sig_callback(DM_PF_ATTR_SIG_CHG_PROC callback);
int32_t dp_para_value_reg(uint16_t type, DATA_SET_S *data);
/* 参考平台的直接采集路径，与原生持久化传输分开。仅登记一次；
 * 后端返回 0 表示接收，且必须恰好完成一次；错误表示拒绝，
 * 之后不得再完成。请求会复制。NO_DM 最多等待 1 秒；
 * 超时后保留存储，直到迟到的完成通知到达。
 * 后续事务失败不能回滚已执行的设备直接写入。
 */
int32_t dp_para_reg_set_func(PARA_SET_FUNC callback);
int32_t dp_set_para_not_dm(PARA_DATA_S *data, PARA_SET_STATUS status);
/* 仅供回调使用的标志，在事务成功提交后生效。 */
void dp_flush_para_store(void);
void dp_set_reboot(void);
/* 要求 DP_PARAMS；刷新存储后安排 100 ms 后复位。 */
void dp_reboot_system(void);
/* 仅处理 RESTORE 参数。两者均遵守默认值策略；
 * 恢复出厂还会刷新持久存储，但不擦除分区或隐式复位。
 * 旧版 void 接口通过日志报告失败，原生接口返回 errno。
 */
void dp_load_def_data(void);
void dp_reset_factory(void);
#endif
