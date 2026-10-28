/* 当前产品只运行本地 process；通信模块不参与本构建。 */
#include "inc/protocol.h"
#include "inc/signal_ids.h"
#include <dataprocess/points.h>

extern const dp_table_t protocol_local_table;

int protocol_init(void)
{
    return dp_register_table(DEVICE_LOCAL, &protocol_local_table);
}
