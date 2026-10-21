#include "ports.h"
#include "process/idu_process.h"
#include <transmit/server.h>
#include <transmit/registers.h>
#include <zephyr/device.h>

/* 协议始终运行在 RAW；实际串口选择与收发回调归 ports.c。 */
const struct tr_server_config idu_forwarding = {
#if DT_HAS_ALIAS(idu_forward)
    .iface_name = "RAW_1",
    .param = {.mode = MODBUS_MODE_RAW},
#endif
    .write = tr_register_write,
    .attach = tr_register_attach, .protocol = &idu_parameter_registers,
#if defined(CONFIG_IDU_AIDC_PROFILE)
    .configure = idu_forwarding_settings,
#endif
    .recovery_timeout_ms = 20000,
};
