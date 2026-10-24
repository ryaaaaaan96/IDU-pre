#include <dataprocess/table_registry.h>
/* 可写目标的周期为零表示仅用于命令。 */
static const MODBUS_RTU_TABLE_S points[] = {
    {.sig_id = 2, .reg_addr = 10, .reg_num = 1, .reg_type = 3, .flag = 2, .period_ms = 0},
    {.sig_id = 3, .reg_addr = 11, .reg_num = 1, .reg_type = 3, .flag = 2, .period_ms = 100},
    {.sig_id = 4, .reg_addr = 12, .reg_num = 1, .reg_type = 3, .flag = 2, .period_ms = 2000},
};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, a_collect) = {
    .id = 10, .type = PRO_MB_MASTER, .rows = points, .count = ARRAY_SIZE(points),
};
