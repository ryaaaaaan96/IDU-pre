#include "ports.h"
#include "process/idu_process.h"
#include <transmit/server.h>
#include <transmit/registers.h>
#include <transmit/mvio.h>
#include <zephyr/device.h>

/* native_sim 测试显式选择 RAW 接口；实际产品由设备树选择转发串口。 */
const struct tr_server_config idu_forwarding = {
#if DT_HAS_ALIAS(idu_forward)
    .iface_name = DEVICE_DT_NAME(DT_ALIAS(idu_forward)),
    .param = {
        .mode = MODBUS_MODE_RTU,
        .serial = {
            .baud = DT_PROP(DT_PARENT(DT_ALIAS(idu_forward)), current_speed),
            .parity = UART_CFG_PARITY_NONE, .stop_bits = UART_CFG_STOP_BITS_1,
        },
    },
#endif
    .write = tr_register_write,
#if defined(CONFIG_IDU_AIDC_PROFILE)
    .attach = tr_register_attach, .protocol = &idu_parameter_registers,
    .configure = idu_forwarding_settings,
#else
    .attach = tr_mvio_attach, .protocol = &idu_mvio_protocol,
#endif
    .recovery_timeout_ms = 20000,
};
