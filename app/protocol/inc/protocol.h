#ifndef IDU_PROTOCOL_H
#define IDU_PROTOCOL_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 注册产品点表回调；随后由调用者执行通用模块 dp_init()。 */
int idu_register_point_tables(void);
const DEV_TABLE_S *idu_device_table(void);
uint16_t idu_device_count(void);
const DM_SIG_CFG_S *idu_process_table(uint8_t dev, uint16_t *count);

enum idu_parameter_country {
    IDU_PARAMETER_COUNTRY_COMMON = 0,
    /* 后续在此增加产品国家参数编号，并在 process.c 添加对应差异行。 */
};

/* 按国家查询默认值、范围和主动恢复策略；不修改 DP 或 NVS。
 * 已配置国家的缺项回退通用记录；未知国家返回 -ENOTSUP，
 * 未定义设备/信号返回 -ENOENT，空输出指针返回 -EINVAL；失败不改输出。
 * value 中的数组指针指向只读 Flash，不可写入。
 * 当前 DP 注册回调固定使用 COMMON；此接口尚不提供运行期国家切换。
 */
int idu_point_parameters_get(uint16_t country, uint16_t dev, uint16_t sig,
                             DM_SIG_DATA_S *value, bool *restore);
int idu_point_default(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *value, bool *recover);
bool idu_point_restore(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);

struct tr_server_config;
struct co_port_config;
struct co_bus_cfg;

/* 启动期注册路由和回调，须先于采集/转发启动；可重复调用。 */
int idu_ports_init(void);
extern const struct co_port_config idu_collect_port;
extern const struct co_bus_cfg idu_collect_bus;
extern const struct tr_server_config idu_forwarding;


#endif
