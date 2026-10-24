#ifndef APPLICATION_PROTOCOL_H
#define APPLICATION_PROTOCOL_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 注册产品点表回调；随后由调用者执行通用模块 dp_init()。 */
int protocol_register_point_tables(void);
/* 完成本地协议装配后放行任务，不等待远端上线。 */
int protocol_init(void);
/* 已锁存的本地通信维护故障，不驱动首次启动或采样。 */
int protocol_error(void);
/* Terminal, idempotent, ISR-safe. Allows already admitted operations to finish
 * or leave durable recovery intent; does not suspend threads holding locks. */
void protocol_stop(void);
bool protocol_stopped(void);
/* Publish remote/storage diagnostics to the application status singleton. */
void protocol_update_status(void);
const DEV_TABLE_S *protocol_device_table(void);
uint16_t protocol_device_count(void);
struct tr_forward_cfg;
struct co_port_config;
struct co_bus_cfg;

/* 启动期注册路由和回调，须先于采集/转发启动；可重复调用。 */
int protocol_ports_init(void);
extern const struct co_port_config protocol_collect_port;
extern const struct co_bus_cfg protocol_collect_bus;
extern const struct tr_forward_cfg protocol_forwarding;


#endif
