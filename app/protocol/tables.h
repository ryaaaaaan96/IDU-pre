#ifndef IDU_PROTOCOL_TABLES_H
#define IDU_PROTOCOL_TABLES_H

#include <dataprocess/table_registry.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* rqst_cyl 编码，不是毫秒值；实际周期由 protocol.c 转换。 */
#define IDU_COLLECT_NO_POLL 0U /* 只读点初始化采集，下发点仅按指令写入。 */
#define IDU_COLLECT_SAMPLE  1U /* 使用 CONFIG_IDU_COLLECT_PERIOD_MS。 */

/* 标识只用于设备关联，不依赖链接顺序；条目数量由各表自行计算。 */
enum idu_table_id {
    IDU_TABLE_LOCAL_PROCESS = 1,
    IDU_TABLE_REMOTE_PROCESS,
    IDU_TABLE_COLLECT,
    IDU_TABLE_TRANSMIT,
};
#endif
