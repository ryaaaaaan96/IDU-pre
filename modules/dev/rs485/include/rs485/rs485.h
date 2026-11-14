/** @file @brief 统一软件 GPIO DE 与 UART 硬件 DE 的字节读写。 */
#ifndef RS485_H
#define RS485_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief DE 控制方式；硬件模式要求 UART 驱动及板级引脚支持。 */
typedef enum {
    RS485_DE_SOFTWARE,
    RS485_DE_HARDWARE,
} rs485_de_mode_t;

/** @brief 接收通知；调用方须在回调内通过 read 排空可读字节，不得阻塞。 */
typedef void (*rs485_rx_ready_t)(void *context);

typedef struct {
    const struct device *uart;
    rs485_de_mode_t de_mode;
    struct gpio_dt_spec direction; /**< 仅软件 DE 使用，逻辑 1 表示发送。 */
    rs485_rx_ready_t rx_ready;     /**< 仅 interrupt 后端支持；async 后端必须为 NULL。 */
    void *rx_context;
} rs485_config_t;

/**
 * @brief 调用方持有的常驻实例，初始化前清零，成员由模块管理。
 * @note 不包含时间戳、协议事件或业务线程；不持有接收缓存。
 */
typedef struct {
    const rs485_config_t *config;
    struct k_sem tx_done;
    struct k_spinlock tx_lock;
    const uint8_t *tx_data;
    size_t tx_remaining;
    int tx_result;
    int fault;
    int rx_error;
    bool tx_busy;
    bool rx_enabled;
    bool rx_blocked;
    bool initialized;
#ifdef CONFIG_RS485_ASYNC
    uart_callback_t callback;
    void *callback_context;
    bool receiving;
    bool rx_starting;
    bool synchronous_tx;
#endif
} rs485_t;

/**
 * @brief 配置 DE 方式并绑定 UART 回调，初始化后接收尚未启动。
 * @note 实例及 config 必须常驻，每 UART 由一个实例独占；成功后不支持重复初始化。
 *       准备好接收处理后使用 rx_enable 或 async rx_start，不直接操作 UART。
 *       硬件 DE 不受驱动支持时返回错误，不回退软件 DE。
 */
int rs485_init(rs485_t *instance, const rs485_config_t *config);
#ifdef CONFIG_RS485_ASYNC
/**
 * @brief 绑定标准 UART 事件回调，不复制接收数据；初始化后、收发前设置。
 * @note 回调上下文由驱动决定，不得阻塞；UART 参数为底层 UART 设备。
 *       TX 终止事件交付前已释放软件 DE；同步 write 的 TX 事件不向外重复交付。
 *       RX_RDY 在发送期间被丢弃；其余缓冲区生命周期事件仍交付，须及时补充缓冲区。
 */
int rs485_callback_set(rs485_t *instance, uart_callback_t callback, void *context);
/** @brief 非阻塞发送；timeout 单位为微秒，SYS_FOREVER_US 表示无限等待。 */
int rs485_tx(rs485_t *instance, const uint8_t *data, size_t size, int32_t timeout_us);
/** @brief 请求中止；缓冲区须保持有效至 TX_DONE/TX_ABORTED，不能仅以返回值判定归还。 */
int rs485_tx_abort(rs485_t *instance);
/**
 * @brief 使用调用方缓冲区启动异步接收；timeout 为 UART 标准微秒超时。
 * @note 直到 RX_BUF_RELEASED 前不得复用缓冲区；超时能力由 UART 驱动决定。
 *       接收回调与同步 read 不可并用；启停及补充缓冲区须由调用方串行组织。
 *       RX_STOPPED 使当前帧作废，后续有效前缀 RX_RDY 也被过滤；调用方须丢弃已收帧。
 */
int rs485_rx_start(rs485_t *instance, uint8_t *buffer, size_t size, int32_t timeout_us);
/** @brief 补充后备接收缓冲区，遵循 UART_RX_BUF_REQUEST/RELEASED 契约。 */
int rs485_rx_buf_rsp(rs485_t *instance, uint8_t *buffer, size_t size);
/** @brief 请求停止接收；等待 RX_DISABLED 及所有 BUF_RELEASED 后才能复用缓冲区。 */
int rs485_rx_stop(rs485_t *instance);
#endif
/** @brief 使能同步读取；async 后端不启动硬件接收，与 rx_start 不可并用。 */
int rs485_rx_enable(rs485_t *instance, bool enabled);
/**
 * @brief 有界清理接收错误及残留，最多读取 256 字节；预算耗尽返回 -EAGAIN，可重试。
 * @note 不改变期望接收状态、不清除发送故障；发送忙或异步 RX 活动时返回 -EBUSY。
 */
int rs485_flush_rx(rs485_t *instance);
/**
 * @brief 读取可用字节并返回数量；不等待填满 capacity，不在模块内缓存。
 * @note async 后端只支持无异步 RX 活动时的线程轮询；rx_ready 必须为 NULL。
 *       rx_ready 为 NULL 时在线程中轮询，支持 K_NO_WAIT、有限超时和 K_FOREVER。
 *       无数据时 K_NO_WAIT 返回 0，有限超时返回 -ETIMEDOUT；轮询按系统 tick 休眠。
 *       配置 rx_ready 时仅在接收通知中使用 K_NO_WAIT 排空 FIFO，禁止阻塞等待。
 *       UART 接收错误返回 -EIO 并暂停接收，调用方清理软件缓存后 flush_rx 恢复。
 */
int rs485_read(rs485_t *instance, uint8_t *data, size_t capacity, k_timeout_t timeout);
/**
 * @brief 线程中同步发送，成功返回 0；返回前停止使用 data。
 * @note 同实例重叠 write 返回 -EBUSY，忙状态保持到原调用者取走结果。
 *       interrupt 后端暂停硬件接收；async 后端保持 RX 并过滤发送期间的 RX_RDY。
 *       软件 DE 要求 GPIO 可在 UART 回调上下文操作。
 *       正常完成在 UART 回调中释放 DE，再唤醒调用方。
 *       interrupt 超时停止填充并释放软件 DE；async 等待中止事件，硬件 DE 由 UART 控制。
 *       超时及已提交发送的终止错误锁存故障，须重启恢复；async 提交失败直接返回错误。
 *       async 后端超时发起 abort 并等待终止事件，清理可能超过 timeout；输入在返回前不可修改。
 *       不宣称移位寄存器已清空；实例禁止清零后重新初始化，避免旧回调引用失效状态。
 *       仅支持半双工；async 只过滤发送态交付的回显，延迟回显须由板级门控保证。
 *       不支持全双工有效数据并行接收。
 */
int rs485_write(rs485_t *instance, const uint8_t *data, size_t size, k_timeout_t timeout);

#endif
