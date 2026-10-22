#ifndef IDU_PROTOCOL_H
#define IDU_PROTOCOL_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 注册产品点表回调；随后由调用者执行通用模块 dp_init()。 */
int idu_register_point_tables(void);
/* 显式连接采集样本、初始化处理及同步参数下发，先于参数加载。 */
int idu_connect_point_services(void);
void idu_start_point_tasks(void);
void idu_start_collect_task(void);
int idu_start_forwarding(void);
/* 已锁存的本地通信维护故障，不驱动首次启动或采样。 */
int idu_protocol_error(void);
const DEV_TABLE_S *idu_device_table(void);
uint16_t idu_device_count(void);
struct tr_forward_cfg;
struct co_port_config;
struct co_bus_cfg;

/* 启动期注册路由和回调，须先于采集/转发启动；可重复调用。 */
int idu_ports_init(void);
extern const struct co_port_config idu_collect_port;
extern const struct co_bus_cfg idu_collect_bus;
extern const struct tr_forward_cfg idu_forwarding;


#endif
