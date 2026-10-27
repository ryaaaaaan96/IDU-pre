/**
 * @file
 * @brief 产品协议装配和运行维护接口。
 */
#ifndef APPLICATION_PROTOCOL_H
#define APPLICATION_PROTOCOL_H

#include <data_process.h>

/**
 * @brief 注册产品点表回调；随后由调用者执行通用模块 dp_init()。
 */
int protocol_register_point_tables(void);
/**
 * @brief 当前仅同步初始化本地 process；不创建 DP 线程，通信装配暂时停用。
 */
int protocol_init(void);
const DEV_TABLE_S *protocol_device_table(void);
uint16_t protocol_device_count(void);
/* 以下只供暂时停用的旧通信实现及其回归测试使用，当前产品不提供这些符号。 */
/**
 * @brief 已锁存的本地通信维护故障，不驱动首次启动或采样。
 */
int protocol_error(void);
/**
 * @brief 终止操作具有幂等性，可在 ISR 中调用。
 *
 * 允许已准入的操作完成或留下持久恢复意图，不挂起持锁线程。
 */
void protocol_stop(void);
bool protocol_stopped(void);

struct tr_forward_cfg;
struct co_port_config;
struct co_bus_cfg;

/**
 * @brief 启动期注册路由和回调，须先于采集/转发启动；可重复调用。
 */
int protocol_ports_init(void);
extern const struct co_port_config protocol_collect_port;
extern const struct co_bus_cfg protocol_collect_bus;
extern const struct tr_forward_cfg protocol_forwarding;


#endif
