/** @file @brief 设备参数区域、NVS 记录与周期保存线程。 */
#include "dp_internal.h"
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(dp_params, LOG_LEVEL_INF);

/* ==================== 区域布局与容量 ==================== */

/* 总区域：布局标记 | 管理 NVS | 等大小设备槽位 | 不足一槽的预留空间。 */
#define PARAMS_NODE DT_ALIAS(dp_params)
#define PARAMS_SIZE DT_REG_SIZE(PARAMS_NODE)
#define SECTOR_SIZE 4096U
#define LAYOUT_SIZE SECTOR_SIZE
#define MANAGEMENT_OFFSET LAYOUT_SIZE
#define MANAGEMENT_SIZE (2U * SECTOR_SIZE)
#define SLOTS_OFFSET (MANAGEMENT_OFFSET + MANAGEMENT_SIZE)

/* Kconfig 只配置数量；扣除公共区域后，以完整扇区均分设备空间。 */
#define SLOT_COUNT CONFIG_DP_STORAGE_SLOT_COUNT
#define AVAILABLE_SECTORS ((PARAMS_SIZE - SLOTS_OFFSET) / SECTOR_SIZE)
#define SLOT_SECTORS (AVAILABLE_SECTORS / SLOT_COUNT)
#define SLOT_SIZE (SLOT_SECTORS * SECTOR_SIZE)

#define MAP_ACTIVE 1U
#define MAP_DELETING 2U
#define RECORD_HEADER_SIZE 4U

BUILD_ASSERT(PARAMS_SIZE >= SLOTS_OFFSET + SLOT_COUNT * 2U * SECTOR_SIZE,
    "Parameter region must provide at least two sectors per device slot");
BUILD_ASSERT(SLOT_SECTORS <= UINT16_MAX);
BUILD_ASSERT(SLOT_COUNT < UINT16_MAX);
BUILD_ASSERT(sizeof(((value_t *)0)->bytes) <= UINT8_MAX);

/* ==================== 槽位运行数据 ==================== */

/* 映射由管理 NVS 持久保存；NVS 自行维护地址缓存，不缓存第二份点值。 */
struct dp_storage {
    struct nvs_fs fs;
    uint16_t device_id;
    uint8_t state;
    bool mounted;
    int mapping_error; /**< 映射提交结果不确定，仅隔离本槽位，重启后重新确认。 */
};

static struct dp_storage slots[SLOT_COUNT];
static struct nvs_fs management;
static const struct flash_area *area;
static struct k_thread save_thread;
static K_THREAD_STACK_DEFINE(save_stack, CONFIG_DP_SAVE_STACK_SIZE);

/* ==================== Flash 布局检查 ==================== */

static void configure_fs(struct nvs_fs *fs, off_t relative_offset, size_t size)
{
    memset(fs, 0, sizeof(*fs));
    fs->flash_device = flash_area_get_device(area);
    fs->offset = area->fa_off + relative_offset;
    fs->sector_size = SECTOR_SIZE;
    fs->sector_count = size / SECTOR_SIZE;
}

/* 布局不匹配时重建整个参数区；硬件访问错误直接返回，不作为格式不匹配处理。 */
static int check_layout(bool reset)
{
    if (!flash_area_device_is_ready(area) || area->fa_off % SECTOR_SIZE ||
        area->fa_size % SECTOR_SIZE || flash_area_align(area) > 16) {
        return -EINVAL;
    }
    for (off_t offset = area->fa_off; offset < area->fa_off + area->fa_size;) {
        struct flash_pages_info page;
        int err = flash_get_page_info_by_offs(flash_area_get_device(area), offset, &page);
        if (err) {
            return err;
        }
        if (!page.size || page.size > SECTOR_SIZE || SECTOR_SIZE % page.size ||
            page.start_offset != offset) {
            return -EINVAL;
        }
        offset += page.size;
    }
    uint8_t expected[32] = {'D', 'P', 'S', 'L', 'O', 'T', 'S', 1};
    sys_put_le32(SLOT_SIZE, expected + 8);
    sys_put_le32(SLOT_COUNT, expected + 12);
    sys_put_le32(SECTOR_SIZE, expected + 16);
    sys_put_le32(area->fa_size, expected + 20);
    sys_put_le32(IS_ENABLED(CONFIG_NVS_DATA_CRC), expected + 24);
    sys_put_le32(crc32_ieee(expected, 28), expected + 28);
    uint8_t actual[sizeof(expected)];
    int err = flash_area_read(area, 0, actual, sizeof(actual));
    if (err) {
        return err;
    }
    if (!reset && !memcmp(actual, expected, sizeof(actual))) {
        return 0;
    }
    LOG_WRN("Reinitializing parameter region");
    err = flash_area_erase(area, 0, area->fa_size);
    if (err) {
        return err;
    }
    return flash_area_write(area, 0, expected, sizeof(expected));
}

/* ==================== 槽位映射与删除恢复 ==================== */

static struct dp_storage *find_storage(uint16_t device_id)
{
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (slots[i].device_id == device_id) {
            return &slots[i];
        }
    }
    return NULL;
}

/* invalid 仅表示记录内容非法；NVS 读取失败保持 false，不触发清空。 */
static int load_mappings(bool *invalid)
{
    *invalid = false;
    /* 先校验完整映射，再执行未结束的删除，防止损坏映射触发错误擦除。 */
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        uint8_t record[4];
        ssize_t count = nvs_read(&management, i + 1, record, sizeof(record));
        if (count == -ENOENT) {
            continue;
        }
        if (count < 0) {
            return count;
        }
        if (count != sizeof(record) || record[0] != 1 ||
            (record[1] != MAP_ACTIVE && record[1] != MAP_DELETING)) {
            LOG_ERR("Invalid mapping record in slot %zu", i);
            *invalid = true;
            return -EBADMSG;
        }
        uint16_t device_id = sys_get_le16(record + 2);
        if (!device_id || find_storage(device_id)) {
            LOG_ERR("Invalid device mapping in slot %zu (device %u)", i, device_id);
            *invalid = true;
            return -EBADMSG;
        }
        slots[i].device_id = device_id;
        slots[i].state = record[1];
    }
    return 0;
}

static int save_mapping(struct dp_storage *slot, uint16_t device_id, uint8_t state)
{
    uint8_t record[4] = {1, state};
    sys_put_le16(device_id, record + 2);
    ssize_t result = nvs_write(&management, (slot - slots) + 1, record, sizeof(record));
    if (result < 0) {
        /* 保留设备归属，防止提交结果不确定的槽位被其他设备复用。 */
        slot->device_id = device_id;
        slot->mapping_error = result;
        return result;
    }
    slot->device_id = device_id;
    slot->state = state;
    return 0;
}

static int finish_delete(struct dp_storage *slot)
{
    size_t index = slot - slots;
    int err = flash_area_erase(area, SLOTS_OFFSET + index * SLOT_SIZE, SLOT_SIZE);
    if (err) {
        return err;
    }
    err = nvs_delete(&management, index + 1);
    if (err) {
        return err;
    }
    memset(slot, 0, sizeof(*slot));
    return 0;
}

/* ==================== 周期保存线程 ==================== */

static void save_loop(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sleep(K_MSEC(CONFIG_DP_SAVE_INTERVAL_MS));
        int err = dp_flush();
        if (err) {
            LOG_ERR("Parameter save failed (%d); pending values will be retried", err);
        }
    }
}

/* ==================== 模块内部接口 ==================== */

/* 初始化：确认布局、加载映射、完成中断删除，最后启动保存线程。 */
int dp_params_init(void)
{
    int err = flash_area_open(DT_FIXED_PARTITION_ID(PARAMS_NODE), &area);
    if (err) {
        return err;
    }
    err = check_layout(false);
    if (err) {
        return err;
    }
    /* 最多重建一次；重新加载仍有异常时返回，不反复擦写 Flash。 */
    for (int attempt = 0; attempt < 2; ++attempt) {
        memset(slots, 0, sizeof(slots));
        configure_fs(&management, MANAGEMENT_OFFSET, MANAGEMENT_SIZE);
        err = nvs_mount(&management);
        if (err) {
            return err;
        }
        bool invalid;
        err = load_mappings(&invalid);
        if (!invalid) {
            if (err) {
                return err;
            }
            break;
        }
        if (attempt == 1) {
            return err;
        }
        err = check_layout(true);
        if (err) {
            return err;
        }
    }
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (slots[i].state == MAP_DELETING) {
            err = finish_delete(&slots[i]);
            if (err) {
                return err;
            }
        }
    }
    k_thread_create(&save_thread,
        save_stack,
        K_THREAD_STACK_SIZEOF(save_stack),
        save_loop,
        NULL,
        NULL,
        NULL,
        CONFIG_DP_SAVE_PRIORITY,
        0,
        K_NO_WAIT);
    k_thread_name_set(&save_thread, "dp_save");
    return 0;
}

/* 挂载：定位槽位，新设备先擦除，统一挂载后再提交新映射。 */
int dp_params_attach(dp_registered_table_t *device)
{
    struct dp_storage *slot = find_storage(device->device_id);
    if (slot && slot->mapping_error) {
        return slot->mapping_error;
    }
    bool new_device = (slot == NULL);
    if (new_device) {
        slot = find_storage(0);
        if (!slot) {
            return -ENOSPC;
        }
    } else if (slot->state != MAP_ACTIVE) {
        return -EBUSY;
    }

    off_t offset = SLOTS_OFFSET + (slot - slots) * SLOT_SIZE;
    if (new_device) {
        /* 空闲槽位可能遗留上次分配中断的内容，新设备使用前统一擦除。 */
        int err = flash_area_erase(area, offset, SLOT_SIZE);
        if (err) {
            return err;
        }
    }
    if (new_device || !slot->mounted) {
        configure_fs(&slot->fs, offset, SLOT_SIZE);
        int err = nvs_mount(&slot->fs);
        if (err) {
            return err;
        }
    }
    if (new_device) {
        int err = save_mapping(slot, device->device_id, MAP_ACTIVE);
        if (err) {
            return err;
        }
    }
    slot->mounted = true;
    device->storage = slot;
    return 0;
}

/* 单点读写：仅处理记录编码与 NVS，不在这里加锁或执行业务回调。 */
int dp_params_load(dp_registered_table_t *device, size_t index)
{
    const dp_point_config_t *point = &device->points[index];
    if (!(point->flags & DP_POINT_FLAG_PERSISTENT)) {
        return 0;
    }
    uint8_t record[RECORD_HEADER_SIZE + sizeof(value_t)];
    ssize_t count = nvs_read(&device->storage->fs, point->signal_key, record, sizeof(record));
    if (count < 0) {
        return count;
    }
    if (count != RECORD_HEADER_SIZE + point->size || record[0] != 1 ||
        record[1] != value_storage_type(point->type) || record[2] != point->size || record[3]) {
        return -ESTALE;
    }
    value_t value = {0};
    const uint8_t *payload = record + RECORD_HEADER_SIZE;
    switch (point->type) {
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
        value.u16 = sys_get_le16(payload);
        break;
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        value.u32 = sys_get_le32(payload);
        break;
    case VALUE_TYPE_WORDS:
        for (size_t i = 0; i < point->size / 2; ++i) {
            value.words[i] = sys_get_le16(payload + i * 2);
        }
        break;
    default:
        memcpy(value.bytes, payload, point->size);
        break;
    }
    device->data[index].value = value;
    return 0;
}

int dp_params_write(dp_registered_table_t *device, size_t index, const value_t *value)
{
    const dp_point_config_t *point = &device->points[index];
    uint8_t record[RECORD_HEADER_SIZE + sizeof(value_t)] = {
        1, value_storage_type(point->type), point->size, 0};
    uint8_t *payload = record + RECORD_HEADER_SIZE;
    switch (point->type) {
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
        sys_put_le16(value->u16, payload);
        break;
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        sys_put_le32(value->u32, payload);
        break;
    case VALUE_TYPE_WORDS:
        for (size_t i = 0; i < point->size / 2; ++i) {
            sys_put_le16(value->words[i], payload + i * 2);
        }
        break;
    default:
        memcpy(payload, value->bytes, point->size);
        break;
    }
    ssize_t count = nvs_write(
        &device->storage->fs, point->signal_key, record, RECORD_HEADER_SIZE + point->size);
    return count < 0 ? count : 0;
}

/* 仅删除 Flash 参数及映射，不操作 RAM 点表。 */
int dp_params_remove(uint16_t device_id)
{
    if (device_id == DP_DEVICE_ID_ALL) {
        return flash_area_erase(area, 0, area->fa_size);
    }
    struct dp_storage *slot = find_storage(device_id);
    if (!slot) {
        return 0;
    }
    if (slot->mapping_error) {
        return slot->mapping_error;
    }
    if (slot->state != MAP_DELETING) {
        int err = save_mapping(slot, device_id, MAP_DELETING);
        if (err) {
            return err;
        }
    }
    return finish_delete(slot);
}
