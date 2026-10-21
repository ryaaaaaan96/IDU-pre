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

/* 同型远端设备共用点表。
 * index：LISTIFY 自动传入的序号，从 0 开始。
 * port_id：采集逻辑端口；first_dev_id / first_address：设备 ID / 站号的起始值。
 */
#define IDU_REMOTE_DEVICE(index, port_id, first_dev_id, first_address) \
    { \
        .dev_id = (first_dev_id) + (index), \
        .port = (port_id), \
        .coll_addr = (first_address) + (index), \
        .dev_code = DP_DEVICE_DEFAULT_CODE, \
        .tables = { \
            {.id = IDU_TABLE_REMOTE_PROCESS, .type = PRO_DATA_PROCESS}, \
            {.id = IDU_TABLE_COLLECT, .type = PRO_MB_MASTER}, \
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE}, \
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE}, \
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE}, \
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE}, \
            {.id = DP_BINDING_UNUSED_ID, .type = DP_BINDING_UNUSED_TYPE}, \
        }, \
    }

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
    /* LISTIFY 参数依次为：
     * 2：生成两项，index 分别为 0、1。
     * IDU_REMOTE_DEVICE：逐项调用的设备初始化宏。
     * (,)：展开项之间的逗号，括号用于将其作为一个宏参数传递。
     * 0：传给 port_id，两台共用采集端口 0。
     * IDU_DEV_REMOTE_IO：传给 first_dev_id，设备 ID 从 10 开始。
     * 1：传给 first_address，通信站号从 1 开始。
     */
    LISTIFY(2, IDU_REMOTE_DEVICE, (,), 0, IDU_DEV_REMOTE_IO, 1),
};
static DEV_TABLE_S devices[ARRAY_SIZE(bindings)];

/* getter 返回启动期装配完成的只读视图。 */
const DEV_TABLE_S *idu_device_table(void)
{
    return devices;
}

uint16_t idu_device_count(void) { return ARRAY_SIZE(devices); }

/* 应用只登记配置回调；通用校验、执行视图及数据池初始化由 dp_init() 完成。
 * 在启动 DP、采集和转发线程之前调用，运行期不得重新注册。
 */
int idu_register_point_tables(void)
{
    if (dp_config()) { return -EBUSY; }
    int err = dp_table_registry_bind(bindings, ARRAY_SIZE(bindings), devices);
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
