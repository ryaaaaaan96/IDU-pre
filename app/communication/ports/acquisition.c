#include "communication/ports/idu_acquisition.h"
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
#include "communication/ports/idu_virtual.h"
#endif
#include <zephyr/device.h>
#include <zephyr/modbus/modbus.h>

#if defined(CONFIG_IDU_COLLECT_RTU)
#define COLLECT_NODE DT_ALIAS(idu_collect)
BUILD_ASSERT(DT_NODE_HAS_STATUS(COLLECT_NODE, okay),
             "Add an idu-collect alias to a real zephyr,modbus-serial node");
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_PARENT(DT_ALIAS(idu_forward))),
             "Acquisition and forwarding need separate physical UARTs");
#if DT_HAS_CHOSEN(zephyr_console)
BUILD_ASSERT(!DT_SAME_NODE(DT_PARENT(COLLECT_NODE), DT_CHOSEN(zephyr_console)),
             "Acquisition must not take over the console UART");
#endif
#endif

int idu_acquisition_port_init(void)
{
#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
    return idu_virtual_port_init();
#else
    int iface = modbus_iface_get_by_name(DEVICE_DT_NAME(COLLECT_NODE));
    if (iface < 0) {
        return iface;
    }
    const struct modbus_iface_param param = {
        .mode = MODBUS_MODE_RTU,
        .rx_timeout = CONFIG_CO_TIMEOUT_MS * 1000U, /* API uses us. */
        .serial = {
            .baud = DT_PROP(DT_PARENT(COLLECT_NODE), current_speed),
            .parity = UART_CFG_PARITY_NONE,
            .stop_bits = UART_CFG_STOP_BITS_1,
        },
    };
    int err = modbus_init_client(iface, param);
    return err ? err : iface;
#endif
}
