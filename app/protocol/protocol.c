/* 先登记 DP，再由 APP 接入可选通信端口。 */
#include "inc/protocol.h"
#ifdef CONFIG_IDU_TRANSMIT
#include "port.h"
#endif
#include "inc/signal_keys.h"
#include "inc/process_table.h"

int protocol_init(void)
{
    int err = dp_init();
    if (err) {
        return err;
    }
    err = dp_register_device(DEVICE_LOCAL, &protocol_local_table);
    if (err) {
        return err;
    }
#ifdef CONFIG_IDU_TRANSMIT
    return protocol_port_init();
#else
    return 0;
#endif
}
