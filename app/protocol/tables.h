#ifndef PROTOCOL_TABLES_H
#define PROTOCOL_TABLES_H

#include <dataprocess/table_registry.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 标识只用于设备关联，不依赖链接顺序；条目数量由各表自行计算。 */
enum protocol_table_id {
    TABLE_LOCAL_PROCESS = 1,
    TABLE_REMOTE_PROCESS,
    TABLE_COLLECT,
    TABLE_TRANSMIT,
    TABLE_PROTOCOL_SEGMENTS,
};
#endif
