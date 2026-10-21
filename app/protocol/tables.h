#ifndef IDU_PROTOCOL_TABLES_H
#define IDU_PROTOCOL_TABLES_H

#include <dataprocess/table_registry.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 标识只用于设备关联，不依赖链接顺序；条目数量由各表自行计算。 */
enum idu_table_id {
    IDU_TABLE_LOCAL_PROCESS = 1,
    IDU_TABLE_REMOTE_PROCESS,
    IDU_TABLE_COLLECT,
    IDU_TABLE_TRANSMIT,
    IDU_TABLE_PROTOCOL_SEGMENTS,
};
#endif
