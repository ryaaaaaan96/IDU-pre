/* 当前产品只运行本地 process；通信模块不参与本构建。 */
#include "inc/protocol.h"
#include "tables.h"
#include <dataprocess/points.h>
#include <zephyr/storage/flash_map.h>

int protocol_init(void)
{
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));

    return dp_start(&protocol_local_model, 1, storage_id);
}
