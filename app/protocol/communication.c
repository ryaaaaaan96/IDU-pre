#include "inc/communication.h"
#include "inc/protocol.h"
#include "rs485_port.h"
#include "status/inc/status.h"
#include <transmit/transmit.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/sys/sys_heap.h>
#endif

LOG_MODULE_REGISTER(app_protocol, LOG_LEVEL_INF);

#define RS485_GAP_US(baud) ((baud) > 19200 ? 1750 : DIV_ROUND_UP(35000000U, (baud)))
#define RS485_BAUD DT_PROP(DT_ALIAS(rs485_1), current_speed)
#define PROTOCOL_STACK_SIZE 8192U

/* 产品装配数量由此表决定，模块不限制通道数量。 */
static const struct {
    rs485_config_t hardware;
    vport_channel_id_t channel_id;
    bool upgrade;
    const char *thread_name;
} channel_configs[] = {
    {.hardware = {.uart = DEVICE_DT_GET(DT_ALIAS(rs485_1)),
         .de_mode = RS485_DE_SOFTWARE,
         .direction = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), rs485_1_de_gpios)},
        .channel_id = RS485_1_CHANNEL_ID,
        .thread_name = "protocol-1"},
    {.hardware = {.uart = DEVICE_DT_GET(DT_ALIAS(rs485_2)),
         .de_mode = RS485_DE_SOFTWARE,
         .direction = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), rs485_2_de_gpios)},
        .channel_id = RS485_2_CHANNEL_ID,
        .upgrade = IS_ENABLED(CONFIG_IDU_IAPV3),
        .thread_name = "protocol-2"},
    {.hardware = {.uart = DEVICE_DT_GET(DT_ALIAS(rs485_3)),
         .de_mode = RS485_DE_SOFTWARE,
         .direction = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), rs485_3_de_gpios)},
        .channel_id = RS485_3_CHANNEL_ID,
        .thread_name = "protocol-3"},
};

typedef struct {
    rs485_port_t port;
    struct k_thread thread;
} communication_channel_t;

static communication_channel_t channels[ARRAY_SIZE(channel_configs)];
static K_THREAD_STACK_ARRAY_DEFINE(
    protocol_stacks, ARRAY_SIZE(channel_configs), PROTOCOL_STACK_SIZE);
static vport_t protocol_vport;
static atomic_t protocol_ready;
static atomic_t initialization_claimed;

static void run(void *p1, void *p2, void *p3)
{
    communication_channel_t *channel = p1;
    bool reported_offline = false;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    while (true) {
        int err = vport_poll(&protocol_vport, channel->port.channel.channel_id);
        if (err == -ENETDOWN) {
            if (!reported_offline) {
                LOG_ERR("Channel %u offline; restart required", channel->port.channel.channel_id);
                reported_offline = true;
            }
            k_msleep(100);
            continue;
        }
        if (err && err != -EIO && err != -EAGAIN && err != -ECANCELED) {
            LOG_WRN("Channel %u poll failed (%d)", channel->port.channel.channel_id, err);
        }
    }
}

/** @brief 装配失败只撤销通信资源；静态端口不支持重新初始化。 */
static void cleanup(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        if (channels[i].port.transport.initialized) {
            (void)rs485_port_stop(&channels[i].port);
        }
    }
    if (protocol_vport.state == VPORT_READY) {
        (void)vport_stop(&protocol_vport);
    }
    protocol_deinit();
    (void)vport_reset(&protocol_vport);
}

static int init_ports(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        int err = rs485_port_init(&channels[i].port,
            &protocol_vport,
            &channel_configs[i].hardware,
            channel_configs[i].channel_id);
        if (err) {
            return err;
        }
        /* 共享 engine 使用同一组帧时序，波特率不同也须满足相同 gap。 */
        if (RS485_GAP_US(channels[i].port.baudrate) != RS485_GAP_US(RS485_BAUD)) {
            return -EINVAL;
        }
    }
    return 0;
}

static int start_channels(void)
{
    int err = vport_start(&protocol_vport);
    if (err) {
        return err;
    }
    /* 全部 RX 成功后才创建线程，确保第一次 poll 时通道已可用。 */
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        err = rs485_port_start(&channels[i].port);
        if (err) {
            return err;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        k_thread_create(&channels[i].thread,
            protocol_stacks[i],
            K_THREAD_STACK_SIZEOF(protocol_stacks[i]),
            run,
            &channels[i],
            NULL,
            NULL,
            8,
            0,
            K_FOREVER);
        k_thread_name_set(&channels[i].thread, channel_configs[i].thread_name);
    }
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        k_thread_start(&channels[i].thread);
    }
    return 0;
}

int communication_init(void)
{
    /* 静态装配仅允许一次，拒绝重复调用，避免清理正在运行的线程资源。 */
    if (!atomic_cas(&initialization_claimed, 0, 1)) {
        return -EALREADY;
    }
    int err = init_ports();
    protocol_channel_config_t configs[ARRAY_SIZE(channels)];
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        configs[i] = (protocol_channel_config_t){
            .channel_id = channel_configs[i].channel_id,
            .upgrade = channel_configs[i].upgrade,
        };
    }
    if (!err) {
        err =
            protocol_init(&protocol_vport, configs, ARRAY_SIZE(configs), RS485_GAP_US(RS485_BAUD));
    }
    if (!err) {
        err = start_channels();
    }
    if (err) {
        cleanup();
        return err;
    }
    atomic_set(&protocol_ready, 1);
    LOG_INF("Modbus unit %u ready on %u channels", protocol_unit(), (unsigned)ARRAY_SIZE(channels));
    return 0;
}

#ifdef CONFIG_SHELL
/** @brief 仅查询独立字段快照，不持有收发锁打印，不消费队列或清零计数。 */
static int protocol_stats(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!atomic_get(&protocol_ready)) {
        shell_warn(shell, "Protocol is not ready");
        return -EAGAIN;
    }
    shell_print(shell,
        "Protocol: channels=%u unit=%u phase=%u",
        (unsigned)ARRAY_SIZE(channels),
        protocol_unit(),
        (unsigned)status_phase_get());
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        rs485_port_t *port = &channels[i].port;
        const char *state = atomic_get(&port->offline)   ? "offline"
                            : atomic_get(&port->rx_lost) ? "recovering"
                                                         : "online";
        shell_print(shell,
            "channel=%u uart=%s baud=%u state=%s iap=%s",
            port->channel.channel_id,
            port->config.uart->name,
            port->baudrate,
            state,
            channel_configs[i].upgrade ? "yes" : "no");
        shell_print(shell,
            "  rx_bytes=%u rx_dropped=%u tx_bytes=%u tx_frames=%u",
            (unsigned)atomic_get(&port->rx_bytes),
            (unsigned)atomic_get(&port->rx_dropped),
            (unsigned)atomic_get(&port->tx_bytes),
            (unsigned)atomic_get(&port->tx_frames));
        shell_print(shell,
            "  rx_recovery_attempts=%u recoveries=%u tx_errors=%u queue=%u/%u queue_peak=%u",
            (unsigned)atomic_get(&port->rx_errors),
            (unsigned)atomic_get(&port->recoveries),
            (unsigned)atomic_get(&port->tx_errors),
            k_msgq_num_used_get(&port->received),
            RS485_RX_QUEUE_SIZE,
            (unsigned)atomic_get(&port->queue_peak));
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
        size_t unused;
        int err = k_thread_stack_space_get(&channels[i].thread, &unused);
        if (!err) {
            size_t size = channels[i].thread.stack_info.size;
            shell_print(shell,
                "  stack_size=%zu stack_unused=%zu stack_used_peak=%zu",
                size,
                unused,
                size - unused);
        } else {
            shell_print(shell, "  Stack statistics unavailable (%d)", err);
        }
#else
        shell_print(shell, "  Stack statistics disabled");
#endif
    }
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && K_HEAP_MEM_POOL_SIZE > 0
    /* 与内核 heap shell 相同的系统堆；锁内只复制，锁外打印。 */
    extern struct k_heap _system_heap;
    struct sys_memory_stats stats;
    k_spinlock_key_t key = k_spin_lock(&_system_heap.lock);
    int err = sys_heap_runtime_stats_get(&_system_heap.heap, &stats);
    k_spin_unlock(&_system_heap.lock, key);
    if (!err) {
        shell_print(shell,
            "System heap: heap_free=%zu heap_used=%zu heap_peak=%zu",
            stats.free_bytes,
            stats.allocated_bytes,
            stats.max_allocated_bytes);
    } else {
        shell_print(shell, "Heap statistics unavailable (%d)", err);
    }
#else
    shell_print(shell, "Heap statistics disabled");
#endif
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(protocol_commands,
    SHELL_CMD_ARG(
        stats, NULL, "Show read-only channel and memory statistics.", protocol_stats, 1, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(protocol, &protocol_commands, "Product protocol diagnostics.", NULL);
#endif
