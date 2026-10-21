#include "protocol.h"
#include "tables.h"
#include "signal_ids.h"
#include <platform_callback.h>
#include <errno.h>
#include "ports.h"
#include <transmit/registers.h>
#include <zephyr/sys/util.h>
#include <transmit/server.h>
#include <zephyr/device.h>

/* 点表内容常驻 Flash；启动时仅装配小型 DEV_TABLE_S 关联视图。 */
static const struct dp_device_binding bindings[] = {
    {.dev_id = IDU_DEV_LOCAL,
     .tables = {{IDU_TABLE_LOCAL_PROCESS, PRO_DATA_PROCESS},
                {IDU_TABLE_TRANSMIT, PRO_MB_SLAVE},
#if defined(CONFIG_IDU_IAPV3)
                {IDU_TABLE_PROTOCOL_SEGMENTS, PRO_ADDR_SLAVE},
#endif
               }},
    {.dev_id = IDU_DEV_REMOTE_IO, .port = 0, .coll_addr = 1,
     .tables = {{IDU_TABLE_REMOTE_PROCESS, PRO_DATA_PROCESS},
                {IDU_TABLE_COLLECT, PRO_MB_MASTER}}},
};
static DEV_TABLE_S devices[ARRAY_SIZE(bindings)];

/* getter 返回启动期装配完成的只读视图。 */
const DEV_TABLE_S *idu_device_table(void)
{
    return devices;
}

uint16_t idu_device_count(void) { return ARRAY_SIZE(devices); }

/* 将点表的 8 位周期编码转换成实际采集间隔。 */
static int collect_period(uint8_t code, uint32_t *ms)
{
    switch (code) {
    case IDU_COLLECT_NO_POLL: *ms = 0; return 0;
    case IDU_COLLECT_SAMPLE: *ms = CONFIG_IDU_COLLECT_PERIOD_MS; return 0;
    default: return -ENOTSUP;
    }
}

/* 应用只登记配置回调；通用校验、执行视图及数据池初始化由 dp_init() 完成。
 * 在启动 DP、采集和转发线程之前调用，运行期不得重新注册。
 */
int idu_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    int err = dp_table_registry_bind(bindings, ARRAY_SIZE(bindings), devices);
    if (err) { return err; }
    err = dp_register_collect_period(collect_period);
    if (err) { return err; }
    pltcb_reg_cfg_get_dev_tbl(idu_device_table);
    pltcb_reg_cfg_get_dev_num(idu_device_count);
    return 0;
}

/* 状态和命令回调归 process 表；协议段随本地设备统一装配。 */
static const struct tr_register_service idu_registers = {
    .device = &devices[0],
#if defined(CONFIG_IDU_IAPV3)
    .route_broadcast = true,
#endif
};

/* 协议始终运行在 RAW；实际串口选择与收发回调归 ports.c。 */
const struct tr_server_config idu_forwarding = {
#if DT_HAS_ALIAS(idu_forward)
    .iface_name = "RAW_1",
    .param = {.mode = MODBUS_MODE_RAW},
#endif
    .write = tr_register_write,
    .attach = tr_register_attach, .protocol = &idu_registers,
    .recovery_timeout_ms = 20000,
};
