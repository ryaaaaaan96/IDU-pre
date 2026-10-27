/**
 * @file
 * @brief 产品点表的内部声明。
 */
#ifndef PROTOCOL_TABLES_H
#define PROTOCOL_TABLES_H

#include <dataprocess/table_registry.h>
#include <dataprocess/model.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/**
 * @brief 标识只用于设备关联，不依赖链接顺序；条目数量由各表自行计算。
 */
typedef enum {
    TABLE_LOCAL_PROCESS = 1,
    TABLE_REMOTE_PROCESS,
    TABLE_COLLECT,
    TABLE_TRANSMIT,
    TABLE_PROTOCOL_SEGMENTS,
} protocol_table_id_t;

/** @brief 与本地测点表一起编译的只读模型描述，不包含实时值。 */
extern const struct dp_model_cfg protocol_local_model;
#endif
