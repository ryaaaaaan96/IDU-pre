#ifndef CO_H
#define CO_H
#include <dataprocess/points.h>
#include <zephyr/modbus/modbus.h>
#define CO_MAX_REGS DP_TRANSFER_WORDS
enum co_port_mode { CO_PORT_POLL, CO_PORT_INIT, CO_PORT_UPDATE,
                     CO_PORT_IV, CO_PORT_FAULT, CO_PORT_PASSTHROUGH, CO_PORT_SLEEP };
enum co_device_online { CO_DEVICE_UNKNOWN, CO_DEVICE_OFFLINE,
                         CO_DEVICE_ONLINE, CO_DEVICE_EXCEPTION, CO_DEVICE_INITIALIZING };
struct co_device_status {
    uint16_t dev_id;
    uint8_t bus_id;
    bool simulated;
    enum co_device_online state;
    int error;
    uint32_t successes, failures;
    int64_t last_success;
    bool online; /* 已收到有效响应，但初始化仍可能失败。 */
    int last_error; /* 历史状态，后续操作成功也予以保留。 */
    int64_t last_response;
};
struct co_device_policy {
    uint8_t retries; /* 初始化和轮询的额外尝试次数；离线探测不重试。 */
    uint32_t exception_ms;
    uint32_t probe_ms; /* 一次离线探测结束至下一次探测的间隔。 */
};
/* 仅在启动期设置的不可变策略，每个受管设备一份。
 * 未配置策略时仍可显式或手动采集，但不自动发现设备。
 */
int co_set_device_policy(uint16_t dev_id, const struct co_device_policy *policy);
/* 使用应用传输回调时没有 Zephyr 接口；其他可选后端均默认关闭。 */
#define CO_BUS_DEFAULT_IFACE (-1)
#define CO_BUS_DEFAULT_SIMULATED false
#define CO_BUS_DEFAULT_INITIALIZE NULL
#define CO_BUS_DEFAULT_READ_MODEL NULL
#define CO_BUS_DEFAULT_USER NULL

struct co_bus_cfg {
    uint8_t id;
    int iface;
    bool simulated;
    /* 可选协议后端；请求与响应共用缓冲区，在总线锁内调用。
     * 未启用 CO_MODBUS_ADAPTER 时须提供 transfer 和／或 read_model。
     */
    /* 遵守剩余总超时预算。发送前竞争超时返回 EBUSY；
     * ETIMEDOUT 表示请求已发送但没有响应。 */
    int (*transfer)(void *user, uint16_t dev_id, struct modbus_adu *adu,
                    k_timeout_t timeout);
    int (*initialize)(void *user, uint16_t dev_id);
    int (*read_model)(void *user, uint16_t dev_id, uint16_t model,
                       uint16_t offset, uint16_t *words, size_t count);
    void *user;
};
/* 应用显式连接数据消费者；回调在启动任务前复制。
 * sample 必须在返回前复制或接收批次。
 * sync 要求初始化样本处理完成并校验质量。
 */
struct co_data_callbacks {
    int (*sample)(struct dp_point_batch *batch, bool sync);
    /* 仅用于显式 co_initialize_device 的完成通知。
     * 自动发现只读取配置报文，不重放业务 INIT 或已保存的写入。
     */
    int (*initialize)(uint16_t dev_id);
    /* 仅在实际转为离线时调用，并位于 CO 锁外；须保留时间戳。 */
    int (*offline)(uint16_t dev_id, int error, int64_t observed_at);
};
int co_set_data_callbacks(const struct co_data_callbacks *callbacks);
struct dp_params_transport;
/* 可直接登记的可选同步南向参数适配实现。 */
const struct dp_params_transport *co_parameter_transport(void);
struct co_request {
    uint32_t id;
    uint16_t dev_id;
    uint8_t priority;
    enum co_port_mode mode; /* 仅在端口处于此模式时调度。 */
    struct modbus_adu adu;
};
typedef void (*co_done_fn)(const struct co_request *reply,
                                    int error, void *user);
int co_init(const struct dp_equip_cfg *cfg, int iface, bool simulated);
int co_poll(size_t block);
/* 校验并启用已登记总线，不创建线程。随后由应用为每条总线运行一个任务，
 * 传入 (void *)(uintptr_t)bus_id。
 */
int co_polling_init(void);
/* 可在 ISR 中发出终止请求。任务取消排队请求，
 * 当前交换按正常流程释放所有权；切换模式不能恢复已停止的总线。 */
void co_stop(void);
void co_bus_task(void *bus_id, void *unused1, void *unused2);
/* 所有设备的总线 ID 都必须小于 CONFIG_CO_MAX_BUSES，
 * 包括稍后才登记的总线。安装回调前先拒绝无效配置。
 */
int co_init_bus(const struct dp_equip_cfg *cfg, const struct co_bus_cfg *bus);
int co_set_mode(uint8_t bus, enum co_port_mode mode);
int co_get_mode(uint8_t bus, enum co_port_mode *mode);
int co_enable_poll(size_t block, bool enabled);
int co_device_status(uint16_t dev_id, struct co_device_status *status);
/* 仅清除 RAM 错误和请求计数，保留调度与数据质量。 */
int co_clear_device_errors(uint16_t dev_id);
int co_initialize_device(uint16_t dev_id);
/* 将请求复制到有界的总线队列并唤醒任务。已接收的请求只完成一次，
 * 且仅在匹配的端口模式下执行。
 */
int co_submit(const struct co_request *request,
                        co_done_fn done, void *user);
int co_cancel(uint8_t bus); /* 排队请求以 -ECANCELED 完成。 */
int co_transfer_mode(uint16_t dev_id, enum co_port_mode mode, struct modbus_adu *adu);
/* 普通读写要求处于 POLL 模式，或由当前初始化上下文持有设备。 */
int co_transfer(uint16_t dev_id, struct modbus_adu *adu);
int co_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int co_write(uint16_t dev_id, uint16_t address, const uint16_t *words, size_t count);
int co_reconfigure(uint8_t bus, struct modbus_iface_param param);
ZBUS_CHAN_DECLARE(co_device_events);
#endif
