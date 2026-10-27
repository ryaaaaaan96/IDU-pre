#include "protocol.h"
#include "tables.h"
#include "signal_ids.h"
#include <platform_callback.h>
#include <errno.h>
#include "ports.h"
#include <transmit/forward.h>
#include <zephyr/sys/util.h>

/* 同型远端设备共用点表。
 * index：LISTIFY 自动传入的序号，从 0 开始。
 * port_id：采集逻辑端口；first_dev_id / first_address：设备 ID / 站号的起始值。
 */
#define REMOTE_DEVICE_BINDING(index, port_id, first_dev_id, first_address) \
    { \
        .dev_id = (first_dev_id) + (index), \
        .port = (port_id), \
        .coll_addr = (first_address) + (index), \
        .tables = {{TABLE_REMOTE_PROCESS, PRO_DATA_PROCESS}, \
                   {TABLE_COLLECT, PRO_MB_MASTER}}, \
    }

/* 点表常驻 Flash；DP 只装配数据归属及可选南向采集关系。 */
static const struct dp_device_binding bindings[] = {
    {
        .dev_id = DEVICE_LOCAL,
        .port = DP_DEVICE_DEFAULT_PORT,
        .coll_addr = DP_DEVICE_DEFAULT_ADDRESS,
        .tables = {{TABLE_LOCAL_PROCESS, PRO_DATA_PROCESS}},
    },
    /* LISTIFY：2 项；逐项调用 REMOTE_DEVICE_BINDING；(,) 分隔；
     * 其余参数分别为采集逻辑口、首设备 ID、首南向站号。
     * index 从 0 开始，两台共用表，ID 为 10/11、站号为 1/2。
     */
    LISTIFY(2, REMOTE_DEVICE_BINDING, (,), 0, DEVICE_REMOTE_IO, 1),
};
static DEV_TABLE_S devices[ARRAY_SIZE(bindings)];

/* getter 返回启动期装配完成的只读视图。 */
const DEV_TABLE_S *protocol_device_table(void)
{
    return devices;
}

uint16_t protocol_device_count(void) 
{ 
    return ARRAY_SIZE(devices); 
}

/* 应用只登记配置回调；通用校验、执行视图及数据池初始化由 dp_init() 完成。
 * 在启动 DP、采集和转发线程之前调用，运行期不得重新注册。
 */
int protocol_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    int err = dp_table_registry_bind(bindings, ARRAY_SIZE(bindings), devices);
    if (err) { return err; }
    pltcb_reg_cfg_get_dev_tbl(protocol_device_table);
    pltcb_reg_cfg_get_dev_num(protocol_device_count);
    return 0;
}

/* 转发身份独立于 DP 数据设备；北向站号在协议初始化时固定。
 * TR 解释登记表并管理 RAW 服务，应用仅编排物理收发。
 */
const struct tr_forward_cfg protocol_forwarding = {
    .forward_id = 1,
    .port_id = PORT_FORWARD,
    .transmit_table = TABLE_TRANSMIT,
#if defined(CONFIG_IDU_IAPV3)
    .protocol_segments = TABLE_PROTOCOL_SEGMENTS,
    .route_broadcast = true,
#else
    .protocol_segments = TR_FORWARD_DEFAULT_SEGMENTS,
    .route_broadcast = TR_FORWARD_DEFAULT_BROADCAST,
#endif
};
