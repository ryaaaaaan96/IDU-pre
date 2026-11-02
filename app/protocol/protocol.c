/* 先登记 DP，再由 APP 接入通信端口。 */
#include "inc/protocol.h"
#include "port.h"
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
    return protocol_port_init();
}
