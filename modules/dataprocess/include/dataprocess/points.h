/**
 * @file
 * @brief 设备测点存储、质量和样本发布接口。
 */
#ifndef DP_POINTS_H
#define DP_POINTS_H

#include <stddef.h>
#include <stdint.h>
#include <data_process.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#define DP_POINT_WORDS CONFIG_DP_POINT_WORDS
#define DP_BATCH_POINTS 12
#define DP_TRANSFER_WORDS 32

struct dp_key {
    uint16_t dev_id;
    uint16_t sig_id;
};

typedef enum { DP_U16, DP_S16, DP_U32, DP_S32, DP_WORDS, DP_U8 } dp_value_type_t;
typedef enum { DP_SOURCE_CONSTANT, DP_SOURCE_LOCAL, DP_SOURCE_MODBUS,
                  DP_SOURCE_PARAMETER } dp_source_t;
/**
 * @brief 存储中的数值采用低字在前、主机字节序。
 */
typedef enum { DP_LOW_WORD_FIRST, DP_HIGH_WORD_FIRST,
                     DP_LOW_WORD_BYTE_SWAP, DP_HIGH_WORD_BYTE_SWAP } dp_word_order_t;
static inline uint16_t dp_order_word(const uint16_t *words, size_t width,
                                     size_t index, dp_word_order_t order)
{
    uint16_t v = words[(order & 1) ? width - 1 - index : index];
    return (order & 2) ? (uint16_t)((v << 8) | (v >> 8)) : v;
}
typedef enum {
    DP_QUALITY_GOOD,
    DP_QUALITY_UNINITIALIZED,
    DP_QUALITY_COMM_ERROR,
    DP_QUALITY_STALE,
    DP_QUALITY_BAD_VALUE,
} dp_quality_t;
#define DP_QUALITY_SIMULATED 0x0100U

/**
 * @brief 寄存器字采用主机字节序；32 位数值在此处低字在前。
 *
 * DP_WORDS 是不透明字序列，字符串按高字节、低字节排列。
 */
struct dp_value {
    uint16_t words[DP_POINT_WORDS];
};

struct dp_point_cfg;
struct dp_process_ctx;
typedef int (*dp_handler_fn)(struct dp_process_ctx *ctx, struct dp_key key);
/**
 * @brief 快速、非阻塞的纯转换；由处理线程在存储锁内调用， 不得调用测点接口或执行 I/O。
 *
 * 范围检查作用于转换后的数值， 返回负数表示拒绝样本。
 */
typedef int (*dp_process_fn)(const struct dp_point_cfg *cfg,
                             struct dp_value *value,
                             const struct dp_value *old_value);

struct dp_point_cfg {
    struct dp_key key;
    dp_source_t source;
    dp_value_type_t type;
    uint8_t width;
    uint32_t stale_ms; /**< 0 表示不限制数据年龄；常量永不过期。 */
    int64_t minimum;
    int64_t maximum;
    struct dp_value initial; /**< 常量和已初始化参数初始即有效。 */
    dp_process_fn process;
    /**
     * @brief 仅供参数源使用；写入准备必须是非阻塞的纯操作，不得调用参数接口。
     *
     * 转发值必须是绝对且幂等的设定值。
     */
    uint8_t param_flags;
    uint16_t write_address;
    dp_word_order_t write_order;
    dp_process_fn prepare_write;
    /**
     * @brief 持久键缺失时使用初始值，或此可选的只读种子存储。
     *
     * 允许执行 I/O， 返回 0 或 -errno，且不得重入参数接口。
     */
    int (*seed_missing)(const struct dp_point_cfg *cfg, struct dp_value *value);
    /**
     * @brief 事务回调；使用 dp_txn_* 访问器，不得重入引擎。
     */
    dp_handler_fn handler;
    bool read_callback;
    /**
     * @brief 参考字节接口：0 表示使用原生数值或存储宽度。
     */
    uint8_t size_bytes;
    uint32_t reference_data_type;
    DM_PF_SIG_PROC reference_handler;
};

struct dp_device_cfg {
    uint16_t dev_id;
    uint8_t unit_id;
    uint8_t bus_id;
    uint16_t dev_code;
};

struct co_field {
    uint16_t sig_id;
    uint8_t offset;
    uint8_t width;
    dp_word_order_t order;
};

/**
 * @brief 显式连续请求；不隐式跨越未说明的地址空洞合并。
 */
struct co_cfg {
    uint16_t dev_id;
    uint8_t function; /**< 3 or 4 */
    uint16_t address; /**< 原始通信地址，不隐式加减 1。 */
    uint16_t count;
    uint32_t period_ms;
    const struct co_field *fields;
    size_t field_count;
    uint8_t priority; /**< 取值 0..3；同优先级时，最早到期的请求优先。 */
    bool init_only;
    bool model_protocol;
    uint16_t model_id; /**< UPP 模型标识；地址使用原始模型偏移。 */
    /**
     * @brief 用于存放单行不可变参考表视图的临时字段。
     */
    struct co_field reference_field;
};

typedef enum { DP_MAP_VALUE, DP_MAP_QUALITY, DP_MAP_ZERO } dp_map_kind_t;
struct dp_transmit_cfg {
    uint8_t function;
    uint16_t address;
    uint16_t count;
    struct dp_key key;
    dp_map_kind_t kind;
    dp_word_order_t order;
};

struct dp_model_cfg;

struct dp_equip_cfg {
    const struct dp_model_cfg *models;
    size_t model_count;
    const struct dp_point_cfg *points;
    size_t point_count;
    const struct dp_device_cfg *devices;
    size_t device_count;
    const struct co_cfg *collect;
    size_t collect_count;
    const struct dp_transmit_cfg *transmit;
    size_t transmit_count;
    /**
     * @brief 旧版重叠映射要求起始地址精确匹配，并完整读取测点。
     */
    bool exact_overlapping_reads;
    /**
     * @brief 可选原始目录可全部存放在 Flash 中。
     */
    const DEV_TABLE_S *reference_devices;
    size_t reference_device_count;
    /**
     * @brief 可选读取器直接提供常量参考表，避免为每个原生描述符分配副本。
     *
     * 读取器填充调用方的临时空间；配置及默认值、范围提供器必须保持不变。
     */
    int (*point_get)(const struct dp_equip_cfg *, size_t, struct dp_point_cfg *);
    int (*collect_get)(const struct dp_equip_cfg *, size_t, struct co_cfg *);
    int (*transmit_get)(const struct dp_equip_cfg *, size_t, struct dp_transmit_cfg *);
};

const struct dp_point_cfg *dp_config_point(const struct dp_equip_cfg *cfg,
                                          size_t index, struct dp_point_cfg *scratch);
const struct co_cfg *dp_config_collect(const struct dp_equip_cfg *cfg,
                                       size_t index, struct co_cfg *scratch);
const struct dp_transmit_cfg *dp_config_transmit(const struct dp_equip_cfg *cfg,
                                                size_t index, struct dp_transmit_cfg *scratch);
const struct dp_point_cfg *dp_config_find(const struct dp_equip_cfg *cfg,
                                         struct dp_key key, struct dp_point_cfg *scratch);
struct dp_key dp_config_key(const struct dp_equip_cfg *cfg, size_t index);
/**
 * @brief 参考表只搜索匹配设备的表行，不在 RAM 中复制整表。
 */
int dp_config_index(const struct dp_equip_cfg *cfg, struct dp_key key);

struct dp_point_update {
    struct dp_key key;
    struct dp_value value;
    int error; /**< 非零传输错误会将此前的值标为无效。 */
    uint8_t width;
};

struct dp_point_batch {
    int64_t sampled_at;
    uint32_t ticket; /**< 由发布接口赋值，按 FIFO 顺序消费。 */
    dp_source_t source;
    uint16_t flags; /**< 取值为 DP_QUALITY_SIMULATED 或 0。 */
    uint8_t count;
    struct dp_point_update points[DP_BATCH_POINTS];
};

struct dp_point_state {
    struct dp_value value;
    int64_t sampled_at;
    uint32_t generation;
    int error;
    uint16_t quality; /**< 低字节为质量枚举，高位为来源标志。 */
};

/**
 * @brief 设备存储为单例，配置具有静态生命周期。
 *
 * 发布或启动采集前先初始化； 初始化会校验全部三张表。
 */
int dp_validate(const struct dp_equip_cfg *cfg);
int dp_init_config(const struct dp_equip_cfg *cfg);
/**
 * @brief 复位前不可恢复。
 *
 * 可在 ISR 中请求， 不加锁、不分配内存、不执行 I/O。任务取消排队工作，完成已准入操作； 读取和新业务返回 ECANCELED，防止保留缓存被误认为健康数据。
 */
void dp_stop(void);
bool dp_stopped(void);
/**
 * @brief 由应用创建的线程入口；放行线程前先初始化存储。
 */
void dp_samples_task(void *unused1, void *unused2, void *unused3);
/**
 * @brief 本地遥测输入仅在线程中调用，ISR 中返回 -EWOULDBLOCK。
 *
 * 字序遵循 dp_value 布局，不使用参考平台的字节缓冲 ABI。 单点接口不报告采集错误，批次接口可报告失败采集； 仅可写
 * DP_SOURCE_LOCAL 测点， 参数写入和事务派生输出使用各自接口。 count 为 1..DP_BATCH_POINTS，
 * 宽度须匹配配置。 整批校验通过后才复制一条消息入队。返回 0 仅表示已接收， 超时只限制入队；返回后可复用输入内存。非空 ticket
 * 在入口清零， 仅成功时填充。使用 dp_wait_processed() 等待， 再通过 dp_read() 检查质量和错误。
 * 纯转换及 SAMPLE 处理函数在 DP 中执行。
 */
int dp_submit_sig_value(struct dp_key key, const uint16_t *words, size_t word_count,
                     k_timeout_t timeout, uint32_t *ticket);
int dp_submit_sig_values(const struct dp_point_update *values, size_t count,
                      k_timeout_t timeout, uint32_t *ticket);
/**
 * @brief 通用采集输入入口，collect 也用它提交远端样本。
 */
int dp_publish(struct dp_point_batch *batch, k_timeout_t timeout);
/**
 * @brief 由通知驱动的等待，允许多个线程等待相同或不同票据。
 *
 * 已完成票据在 K_NO_WAIT 下也成功。超时不会取消处理； 完成后仍须检查测点质量和错误。
 */
int dp_wait_processed(uint32_t ticket, k_timeout_t timeout);
int dp_read(struct dp_key key, struct dp_point_state *state);
bool dp_healthy(uint32_t max_idle_ms);
/**
 * @brief 未启用 DP_PROCESS 时，仍可访问不可变配置和定义。
 */
const struct dp_equip_cfg *dp_config(void);
/**
 * @brief 返回视图可能使用临时空间；该空间失效后不得继续持有视图。
 */
const struct dp_point_cfg *dp_find(struct dp_key key, struct dp_point_cfg *scratch);
/**
 * @brief 此可合并通知通道使用普通订阅者或监听器。
 *
 * 采集通道及其消息订阅者属于内部实现； 应通过 dp_publish() 发布， 以便在分配 zbus 内存前预留队列容量。
 */
ZBUS_CHAN_DECLARE(dp_processed_chan);
struct dp_processed_event {
    uint32_t generation;
    uint16_t changed;
};

#endif
