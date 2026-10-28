/** @brief process 点表的行索引；数值表示表中顺序，不是 DP 测点键。 */
#ifndef PROCESS_TABLE_H
#define PROCESS_TABLE_H

#include <dataprocess/dataprocess.h>

typedef enum {
    PROCESS_SAMPLE_PERIOD,
    PROCESS_LABEL,
    PROCESS_SAVED_ADDRESS,
    PROCESS_DEVICE_PROPERTIES,
    PROCESS_MANUFACTURER,
    PROCESS_FIRMWARE_VERSION,
    PROCESS_HARDWARE_VERSION,
    PROCESS_DEVICE_TYPE,
    PROCESS_PROTOCOL,
    PROCESS_PRODUCT_CODE,
    PROCESS_VIRTUAL_MODE,
    PROCESS_ADDRESS,
    PROCESS_DI,
    PROCESS_CURRENT_1,
    PROCESS_CURRENT_2,
    PROCESS_CURRENT_3,
    PROCESS_CURRENT_4,
    PROCESS_CURRENT_5,
    PROCESS_CURRENT_6,
    PROCESS_CURRENT_7,
    PROCESS_CURRENT_8,
    PROCESS_SIGNAL_COUNT,
} process_signal_index_t;

/** @brief 本地 process 点表，使用 process_signal_index_t 访问配置行。 */
extern const dp_table_t protocol_local_table;

#endif
