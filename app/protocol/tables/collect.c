#include "protocol.h"
#include "../tables.h"
#include "signal_ids.h"
#include <zephyr/sys/util.h>

/* rqst_cyl = 1 使用 CONFIG_IDU_COLLECT_PERIOD_MS；0 表示初始化读取或仅下发。 */
static const MODBUS_RTU_TABLE_S idu_mvio_collect[] = {
    {.sig_id = IDU_SIG_DI, .reg_addr = 8152, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = 1, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_SIG_CURRENT_1 + 0, .reg_addr = 8163, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = 1, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_SIG_CURRENT_1 + 1, .reg_addr = 8164, .reg_num = 1, .reg_type = 4,
     .rqst_cyl = 1, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_SIG_PROTOCOL, .reg_addr = 8150, .reg_num = 2, .reg_type = 4,
     .rqst_cyl = 0, .byte_order = BYTE_ORDER_DEFAULT},
    {.sig_id = IDU_SIG_ADDRESS, .reg_addr = 30459, .reg_num = 1, .reg_type = 3,
     .rqst_cyl = 0, .byte_order = BYTE_ORDER_DEFAULT},
    /* FLAG_COLLECT process signals use these as explicit write destinations. */
    {.sig_id = IDU_SIG_REMOTE_SETPOINT, .reg_addr = 40000, .reg_num = 1,
     .reg_type = 3, .flag = FLAG_0x03_W_0x06 | FLAG_0x03_w_0x10},
    {.sig_id = IDU_SIG_REMOTE_OFFSET, .reg_addr = 40001, .reg_num = 2,
     .reg_type = 3, .flag = FLAG_0x03_w_0x10},
};

static const STRUCT_SECTION_ITERABLE(dp_registered_table, collect_table) = {
    .id = IDU_TABLE_COLLECT, .type = PRO_MB_MASTER,
    .rows = idu_mvio_collect, .count = ARRAY_SIZE(idu_mvio_collect),
};
