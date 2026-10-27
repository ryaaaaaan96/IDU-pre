/* 当前产品只运行本地 process；通信模块不参与本构建。 */
#include "inc/protocol.h"
#include <dataprocess/model.h>
#include <dataprocess/points.h>
#include <zephyr/storage/flash_map.h>

/* 与本地测点表同编译单元，声明只在使用方保留。 */
extern const struct dp_model_cfg protocol_local_model;

int protocol_init(void)
{
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));

    return dp_start(&protocol_local_model, 1, storage_id);
}
