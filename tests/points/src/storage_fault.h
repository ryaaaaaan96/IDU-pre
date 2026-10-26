#ifndef IDU_TEST_STORAGE_FAULT_H
#define IDU_TEST_STORAGE_FAULT_H

#include <zephyr/storage/flash_map.h>

typedef enum {
    STORAGE_FAULT_NONE,
    STORAGE_FAULT_MOUNT,
    STORAGE_FAULT_INTENT_WRITE,
    STORAGE_FAULT_COMMIT_WRITE,
    STORAGE_FAULT_VALUE_WRITE,
    STORAGE_FAULT_META_WRITE,
    STORAGE_FAULT_COMMIT_DELETE,
    STORAGE_FAULT_INTENT_DELETE,
} storage_fault_stage_t;

/* 仅拦截指定参数分区的外部 NVS 调用；每次 arm 只在选中步骤返回一次 EIO。 */
void storage_fault_reset(const struct flash_area *area);
void storage_fault_arm(storage_fault_stage_t stage);
/* 撤销注入前必须确认目标步骤命中过一次，未命中即使业务返回错误也失败。 */
void storage_fault_finish(void);

#endif
