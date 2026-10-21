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
    {
        .dev_id = IDU_DEV_LOCAL,
        .port = DP_DEVICE_DEFAULT_PORT,
        .coll_addr = DP_DEVICE_DEFAULT_ADDRESS,
        .dev_code = DP_DEVICE_DEFAULT_CODE,
        .tables = {
            {.id = IDU_TABLE_LOCAL_PROCESS, .type = PRO_DATA_PROCESS},
            {.id = IDU_TABLE_TRANSMIT, .type = PRO_MB_SLAVE},
#if defined(CONFIG_IDU_IAPV3)
            {.id = IDU_TABLE_PROTOCOL_SEGMENTS, .type = PRO_ADDR_SLAVE},
#else
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
#endif
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
        },
    },
    {
        .dev_id = IDU_DEV_REMOTE_IO,
        .port = 0,
        .coll_addr = 1,
        .dev_code = DP_DEVICE_DEFAULT_CODE,
        .tables = {
            {.id = IDU_TABLE_REMOTE_PROCESS, .type = PRO_DATA_PROCESS},
            {.id = IDU_TABLE_COLLECT, .type = PRO_MB_MASTER},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE},
        },
    },
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
    .routes = TR_REGISTER_DEFAULT_ROUTES,
    .device = &devices[0],
#if defined(CONFIG_IDU_IAPV3)
    .route_broadcast = true,
#else
    .route_broadcast = TR_REGISTER_DEFAULT_BROADCAST,
#endif
};

/* 协议始终运行在 RAW；实际串口选择与收发回调归 ports.c。 */
const struct tr_server_config idu_forwarding = {
#if DT_HAS_ALIAS(idu_forward)
    .iface_name = "RAW_1",
#else
    .iface_name = TR_SERVER_DEFAULT_IFACE,
#endif
    .param = {
        .mode = MODBUS_MODE_RAW,
        .server = {
            .user_cb = TR_SERVER_DEFAULT_USER_CALLBACK,
            .unit_id = TR_SERVER_DEFAULT_UNIT,
        },
        .rawcb = {
            .raw_tx_cb = TR_SERVER_DEFAULT_RAW_TX,
            .user_data = TR_SERVER_DEFAULT_RAW_USER,
        },
    },
    .write = tr_register_write,
    .attach = tr_register_attach,
    .protocol = &idu_registers,
    .configure = TR_SERVER_DEFAULT_CONFIGURE,
    .recovery_timeout_ms = 20000,
};
