#ifndef IDU_PROTOCOL_H
#define IDU_PROTOCOL_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 注册产品点表回调；随后由调用者执行通用模块 dp_init()。 */
int idu_register_point_tables(void);
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
