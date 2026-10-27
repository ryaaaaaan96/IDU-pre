/* 当前产品只运行本地 process；通信模块不参与本构建。 */
#include "inc/protocol.h"
#include <dataprocess/model.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

/* 与本地测点表同编译单元，声明只在使用方保留。 */
extern const struct dp_model_cfg protocol_local_model;

/* 业务层逐组登记；增加处理表时在这里追加一项。 */
static const struct dp_model_cfg *const product_models[] = {
    &protocol_local_model,
};

int protocol_init(void)
{
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));

    for (size_t i = 0; i < ARRAY_SIZE(product_models); ++i) {
        int err = dp_register_model(product_models[i], storage_id);
        if (err) { return err; }
    }
    return 0;
}
