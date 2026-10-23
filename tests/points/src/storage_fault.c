#include "storage_fault.h"
#include <errno.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

/* 现有参数存储格式的 NVS ID；仅用于定位外部故障，不引用 DP 私有实现。 */
#define SETTINGS_INTENT_ID 2U
#define SETTINGS_META_ID 3U
#define SETTINGS_COMMIT_ID 4U
#define SETTINGS_VALUE_FIRST 0x100U
#define SETTINGS_VALUE_COUNT 1024U

static const char *const stage_names[] = {
    "none", "mount", "intent write", "commit journal write", "value write",
    "metadata write", "commit journal delete", "intent delete",
};

static const struct flash_area *target_area;
static atomic_t target_stage;
static atomic_t hits;

void storage_fault_reset(const struct flash_area *area)
{
    target_area = area;
    atomic_set(&target_stage, STORAGE_FAULT_NONE);
    atomic_clear(&hits);
}

void storage_fault_arm(enum storage_fault_stage stage)
{
    zassert_equal(atomic_get(&target_stage), STORAGE_FAULT_NONE);
    zassert_true(stage > STORAGE_FAULT_NONE && stage <= STORAGE_FAULT_INTENT_DELETE);
    zassert_not_null(target_area);
    atomic_clear(&hits);
    atomic_set(&target_stage, stage);
}

void storage_fault_finish(void)
{
    enum storage_fault_stage stage = atomic_get(&target_stage);
    atomic_set(&target_stage, STORAGE_FAULT_NONE);
    if (stage != STORAGE_FAULT_NONE) {
        zassert_equal(atomic_get(&hits), 1, "Storage fault stage '%s' was not reached",
                      stage_names[stage]);
    }
}

int __real_nvs_mount(struct nvs_fs *fs);
static bool should_fail(struct nvs_fs *fs, enum storage_fault_stage stage)
{
    return stage != STORAGE_FAULT_NONE && target_area &&
           fs->flash_device == flash_area_get_device(target_area) &&
           fs->offset == target_area->fa_off &&
           atomic_get(&target_stage) == stage && atomic_cas(&hits, 0, 1);
}

int __wrap_nvs_mount(struct nvs_fs *fs)
{
    return should_fail(fs, STORAGE_FAULT_MOUNT) ? -EIO : __real_nvs_mount(fs);
}

ssize_t __real_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len);
ssize_t __wrap_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len)
{
    enum storage_fault_stage stage = STORAGE_FAULT_NONE;
    if (id == SETTINGS_INTENT_ID) { stage = STORAGE_FAULT_INTENT_WRITE; }
    else if (id == SETTINGS_COMMIT_ID) { stage = STORAGE_FAULT_COMMIT_WRITE; }
    else if (id == SETTINGS_META_ID) { stage = STORAGE_FAULT_META_WRITE; }
    else if (id >= SETTINGS_VALUE_FIRST && id < SETTINGS_VALUE_FIRST + SETTINGS_VALUE_COUNT) {
        stage = STORAGE_FAULT_VALUE_WRITE;
    }
    if (len && should_fail(fs, stage)) {
        return -EIO;
    }
    return __real_nvs_write(fs, id, data, len);
}

int __real_nvs_delete(struct nvs_fs *fs, uint16_t id);
int __wrap_nvs_delete(struct nvs_fs *fs, uint16_t id)
{
    enum storage_fault_stage stage = STORAGE_FAULT_NONE;
    if (id == SETTINGS_INTENT_ID) { stage = STORAGE_FAULT_INTENT_DELETE; }
    else if (id == SETTINGS_COMMIT_ID) { stage = STORAGE_FAULT_COMMIT_DELETE; }
    if (should_fail(fs, stage)) {
        return -EIO;
    }
    return __real_nvs_delete(fs, id);
}
