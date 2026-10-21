#include <dataprocess/table_registry.h>
static const MB_TABLE_CELL_T points[] = {
    {.dev_id = 1, .sig_id = 1, .reg_addr = 100, .reg_num = 1, .reg_type = 3},
    {.dev_id = 1, .sig_id = 2, .reg_addr = 200, .reg_num = 1, .reg_type = 3},
    {.dev_id = 1, .sig_id = 3, .reg_addr = 201, .reg_num = 1, .reg_type = 3},
    {.dev_id = 1, .sig_id = 4, .reg_addr = 202, .reg_num = 1, .reg_type = 3},
};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, m_transmit) = {
    .id = 30, .type = PRO_MB_SLAVE, .rows = points, .count = ARRAY_SIZE(points),
};
