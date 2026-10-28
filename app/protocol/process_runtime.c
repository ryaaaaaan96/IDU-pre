/* 当前产品只运行本地 process；通信模块不参与本构建。 */
#include "inc/protocol.h"
#include <dataprocess/points.h>
#include <zephyr/sys/util.h>

/* 与本地测点表同编译单元，声明只在使用方保留。 */
extern const struct dp_table protocol_local_table;

/* 业务层逐组登记；增加处理表时在这里追加一项。 */
static const struct dp_table *const product_tables[] = {
    &protocol_local_table,
};

int protocol_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(product_tables); ++i) {
        int err = dp_register_table(product_tables[i]);
        if (err) { return err; }
    }
    return 0;
}
