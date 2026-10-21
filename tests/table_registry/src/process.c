#include <dataprocess/table_registry.h>
static const DM_SIG_CFG_S points[] = {
    {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_PERSISTENCE | FLAG_PERSISTENCE_SYNC},
    {.sig_id = 2, .sig_size = 2, .data_type = DATA_TYPE_U16, .sig_flag = FLAG_COLLECT},
    {.sig_id = 3, .sig_size = 2, .data_type = DATA_TYPE_U16, .sig_flag = FLAG_COLLECT},
    {.sig_id = 4, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_COLLECT | FLAG_PERSISTENCE | FLAG_PERSISTENCE_SYNC},
};
static const STRUCT_SECTION_ITERABLE(dp_registered_table, z_process) = {
    .id = 20, .type = PRO_DATA_PROCESS, .rows = points, .count = ARRAY_SIZE(points),
};
