#include "../tables.h"
#include <iap/iap.h>

/* 实际线地址；IAP 载荷为变长数据，不按寄存器数量限制段的结束位置。 */
static const MB_TABLE_SEG_T protocol_segment_rows[] = {
    {.reg_addr = 20003, .end_addr = 20003, .func = iap_route},
    {.reg_addr = 20049, .end_addr = 20104, .func = iap_route},
    {.reg_addr = 31996, .end_addr = 31997, .func = iap_route},
};

static const STRUCT_SECTION_ITERABLE(dp_registered_table, protocol_segments) = {
    .id = TABLE_PROTOCOL_SEGMENTS, .type = PRO_ADDR_SLAVE,
    .rows = protocol_segment_rows, .count = ARRAY_SIZE(protocol_segment_rows),
};
