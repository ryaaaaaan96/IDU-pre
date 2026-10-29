/** @file @brief 参数持久化、并发保存及跨启动恢复验证。 */
#include "dp_internal.h"
#include <errno.h>
#include <setjmp.h>
#include <zephyr/sys/reboot.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

static jmp_buf reboot_jump;
static bool reboot_expected, reset_callback_called;

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    zassert_true(reboot_expected);
    zassert_equal(type, SYS_REBOOT_COLD);
    longjmp(reboot_jump, 1);
}

static bool fail_write, mutate_write, fail_erase;
static bool commit_point_then_fail;
static int point_read_error;
static bool access_during_erase;
static int point_writes;
static bool fail_layout_read, fail_layout_erase, fail_layout_write;
static int layout_erases;
static bool fail_mapping_read, force_invalid_mapping;
static bool fail_delete, fail_map, block_write, commit_map_then_fail;
static K_SEM_DEFINE(write_entered, 0, 1);
static K_SEM_DEFINE(write_continue, 0, 1);
static K_SEM_DEFINE(remove_finished, 0, 1);
static K_THREAD_STACK_DEFINE(flush_stack, 2048);
static K_THREAD_STACK_DEFINE(remove_stack, 2048);
static struct k_thread flush_thread, remove_thread;
static int flush_result, remove_result;

static void flush_entry(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    flush_result = dp_flush();
}

static void before_reboot(void *context)
{
    zassert_equal_ptr(context, &reset_callback_called);
    zassert_false(reset_callback_called);
    /* 回调在设备锁外，可等待另一线程完成 flush。 */
    k_thread_create(&flush_thread,
        flush_stack,
        K_THREAD_STACK_SIZEOF(flush_stack),
        flush_entry,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_thread_join(&flush_thread, K_SECONDS(1)));
    zassert_ok(flush_result);
    reset_callback_called = true;
}

static void remove_entry(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    remove_result = dp_remove_device(101);
    k_sem_give(&remove_finished);
}

static int write_callback(const dp_callback_context_t *context)
{
    zassert_true(context->is_write);
    if (context->value->u16 == 900) {
        context->value->u16 = 901;
    }
    return 0;
}

static int read_callback(const dp_callback_context_t *context)
{
    zassert_false(context->is_write);
    if (context->value->u16 == 902) {
        context->value->u16 = 903;
    }
    return 0;
}

static const dp_point_config_t rows[] = {
    {.signal_key = 300,
        .write_callback = write_callback,
        .read_callback = read_callback,
        .size = 2,
        .type = VALUE_TYPE_U16,
        .flags = DP_POINT_FLAG_PERSISTENT | DP_POINT_FLAG_CHECK_RANGE,
        .data = {.initial.u16 = 10, .minimum.u16 = 1, .maximum.u16 = 1000}},
    {.signal_key = 99, .size = 2, .type = VALUE_TYPE_U16, .data.initial.u16 = 7},
    {.signal_key = 500,
        .size = 4,
        .type = VALUE_TYPE_WORDS,
        .flags = DP_POINT_FLAG_PERSISTENT | DP_POINT_FLAG_SKIP_DEFAULTS,
        .data.initial.words = {0x1234, 0x5678}},
    {.signal_key = 501,
        .size = 4,
        .type = VALUE_TYPE_S32,
        .flags = DP_POINT_FLAG_PERSISTENT,
        .data.initial.s32 = -100},
};
static const dp_table_t table = {.points = rows, .point_count = ARRAY_SIZE(rows)};
static const dp_table_t reduced = {.points = rows, .point_count = 1};

ssize_t __real_nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t len);

ssize_t __wrap_nvs_read(struct nvs_fs *fs, uint16_t id, void *data, size_t len)
{
    if (fs->offset == 4096) {
        if (fail_mapping_read) {
            return -EIO;
        }
        if (force_invalid_mapping && id == 1) {
            const uint8_t invalid[4] = {1, 1, 0, 0};
            zassert_true(len >= sizeof(invalid));
            memcpy(data, invalid, sizeof(invalid));
            return sizeof(invalid);
        }
    }
    if (fs->offset >= 12288 && point_read_error) {
        return point_read_error;
    }
    return __real_nvs_read(fs, id, data, len);
}

ssize_t __real_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len);

ssize_t __wrap_nvs_write(struct nvs_fs *fs, uint16_t id, const void *data, size_t len)
{
    if (fs->offset == 4096) {
        if (fail_map) {
            return -EIO;
        }
        if (commit_map_then_fail) {
            ssize_t result = __real_nvs_write(fs, id, data, len);
            return result < 0 ? result : -EIO;
        }
    }
    if (fs->offset >= 12288) {
        ++point_writes;
        if (block_write) {
            block_write = false;
            k_sem_give(&write_entered);
            zassert_ok(k_sem_take(&write_continue, K_SECONDS(1)));
        }
        if (fail_write) {
            return -EIO;
        }
        if (commit_point_then_fail) {
            ssize_t result = __real_nvs_write(fs, id, data, len);
            return result < 0 ? result : -EIO;
        }
        if (mutate_write) {
            mutate_write = false;
            const value_t value = {.u16 = 31};
            zassert_ok(dp_set_para(101, 300, &value));
        }
    }
    return __real_nvs_write(fs, id, data, len);
}

int __real_nvs_delete(struct nvs_fs *fs, uint16_t id);

int __wrap_nvs_delete(struct nvs_fs *fs, uint16_t id)
{
    if (fail_delete) {
        return -EIO;
    }
    return __real_nvs_delete(fs, id);
}

int __real_flash_area_erase(const struct flash_area *area, off_t offset, size_t size);

int __wrap_flash_area_erase(const struct flash_area *area, off_t offset, size_t size)
{
    if (!offset && size == area->fa_size) {
        ++layout_erases;
        if (fail_layout_erase) {
            return -EIO;
        }
    }
    if (access_during_erase && (offset >= 12288 || (!offset && size == area->fa_size))) {
        access_during_erase = false;
        /* 擦除时设备锁已被持有，业务读写及其回调仍应完成。 */
        const value_t value = {.u16 = 900};
        value_t output = {0};
        zassert_ok(dp_set_para(101, 300, &value));
        zassert_ok(dp_get_para(101, 300, &output));
        zassert_equal(output.u16, 901);
    }
    if (fail_erase && offset >= 12288) {
        return -EIO;
    }
    return __real_flash_area_erase(area, offset, size);
}

int __real_flash_area_read(const struct flash_area *area, off_t offset, void *data, size_t size);

int __wrap_flash_area_read(const struct flash_area *area, off_t offset, void *data, size_t size)
{
    if (fail_layout_read && !offset) {
        return -EIO;
    }
    return __real_flash_area_read(area, offset, data, size);
}

int __real_flash_area_write(
    const struct flash_area *area, off_t offset, const void *data, size_t size);

int __wrap_flash_area_write(
    const struct flash_area *area, off_t offset, const void *data, size_t size)
{
    if (fail_layout_write && !offset) {
        return -EIO;
    }
    return __real_flash_area_write(area, offset, data, size);
}

static dp_point_data_t *point(uint16_t device_id, size_t index)
{
    for (dp_registered_table_t *device = dp_tables(); device; device = device->next) {
        if (device->device_id == device_id) {
            return &device->data[index];
        }
    }
    return NULL;
}

static void set(uint16_t device_id, uint16_t value)
{
    const value_t input = {.u16 = value};
    zassert_ok(dp_set_para(device_id, 300, &input));
}

static uint16_t get(uint16_t device_id)
{
    value_t output;
    zassert_ok(dp_get_para(device_id, 300, &output));
    return output.u16;
}

static void exercise(void)
{
    dp_point_config_t reserved = rows[0];
    reserved.signal_key = UINT16_MAX;
    const dp_table_t reserved_table = {.points = &reserved, .point_count = 1};
    zassert_equal(dp_register_device(900, &reserved_table), -EINVAL);
    reserved.flags = 0; /* NVS 保留 ID 不限制纯 RAM 点。 */
    zassert_ok(dp_register_device(900, &reserved_table));
    zassert_is_null(point(900, 0)->saved_value);
    zassert_ok(dp_remove_device(900));
    zassert_ok(dp_register_device(101, &table));
    zassert_ok(dp_register_device(102, &table));
    zassert_false(point(101, 0)->dirty);
    zassert_not_null(point(101, 0)->saved_value);
    zassert_true(point(101, 0)->saved_valid);
    zassert_is_null(point(101, 1)->saved_value);
    zassert_ok(dp_flush());
    zassert_equal(point_writes, 6); /* 两设备各追加三个初值，flush 不再重复写。 */
    set(101, 10);
    zassert_false(point(101, 0)->dirty);
    set(101, 20);
    set(101, 30);
    fail_write = true;
    zassert_equal(dp_flush(), -EIO);
    zassert_true(point(101, 0)->dirty);
    zassert_equal(get(101), 30);
    fail_write = false;
    mutate_write = true;
    zassert_ok(dp_flush());
    zassert_true(point(101, 0)->dirty);
    zassert_equal(get(101), 31);
    zassert_ok(dp_flush());
    zassert_false(point(101, 0)->dirty);
    zassert_true(point(101, 0)->saved_valid);
    zassert_equal(point(101, 0)->saved_value->u16, 31);
    int writes = point_writes;
    set(101, 40);
    zassert_true(point(101, 0)->dirty);
    set(101, 31); /* 保存前回到 Flash 中的值，不必重复写入。 */
    zassert_false(point(101, 0)->dirty);
    set(101, 31);
    zassert_ok(dp_flush());
    zassert_equal(point_writes, writes);
    set(101, 40);
    mutate_write = true; /* Flash 写 40 时，RAM 又回到先前保存的 31。 */
    zassert_ok(dp_flush());
    zassert_equal(get(101), 31);
    zassert_equal(point(101, 0)->saved_value->u16, 40);
    zassert_true(point(101, 0)->dirty);
    zassert_ok(dp_flush());
    zassert_false(point(101, 0)->dirty);
    zassert_equal(point(101, 0)->saved_value->u16, 31);
    zassert_equal(get(102), 10);
    set(101, 900);
    zassert_equal(get(101), 901);
    zassert_ok(dp_flush());
    set(101, 902);
    zassert_equal(get(101), 903);
    zassert_true(point(101, 0)->dirty);
    zassert_ok(dp_flush());
    const value_t words = {.words = {0xabcd, 0xef01}};
    zassert_ok(dp_set_para(101, 500, &words));
    zassert_ok(dp_restore_defaults(101));
    zassert_equal(get(101), 10);
    value_t output;
    zassert_ok(dp_get_para(101, 500, &output));
    zassert_equal(output.words[0], 0xabcd);
    zassert_ok(dp_flush());
    /* 阻塞 Flash 保存时删除必须等待，但普通 RAM 更新仍能完成。 */
    set(101, 32);
    block_write = true;
    k_thread_create(&flush_thread,
        flush_stack,
        K_THREAD_STACK_SIZEOF(flush_stack),
        flush_entry,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_sem_take(&write_entered, K_SECONDS(1)));
    set(101, 33);
    zassert_equal(get(101), 33);
    /* 从这里起不再访问设备，满足移除前业务静止约定。 */
    k_thread_create(&remove_thread,
        remove_stack,
        K_THREAD_STACK_SIZEOF(remove_stack),
        remove_entry,
        NULL,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_equal(k_sem_take(&remove_finished, K_MSEC(1)), -EAGAIN);
    k_sem_give(&write_continue);
    zassert_ok(k_thread_join(&flush_thread, K_SECONDS(1)));
    zassert_ok(k_thread_join(&remove_thread, K_SECONDS(1)));
    zassert_ok(flush_result);
    zassert_ok(remove_result);
    zassert_is_null(point(101, 0));
    zassert_ok(dp_remove_device(101));
    zassert_ok(dp_register_device(101, &table));
    zassert_equal(get(101), 10);
    zassert_ok(dp_remove_device(101));
    zassert_ok(dp_remove_device(102));
}

ZTEST(dp_storage, test_storage)
{
    const char *phase = getenv("IDU_DP_PHASE");
    if (!phase) {
        phase = "exercise";
    }
    if (!strcmp(phase, "unknown") || !strcmp(phase, "layout_read_error") ||
        !strcmp(phase, "layout_erase_error") || !strcmp(phase, "layout_write_error")) {
        const struct flash_area *area;
        zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(dp_params)), &area));
        uint8_t unknown[32] = {0};
        uint8_t outside[32] = {0x12, 0x34};
        zassert_ok(flash_area_write(area, 0, unknown, sizeof(unknown)));
        zassert_ok(flash_area_write(area, area->fa_size - 32, unknown, sizeof(unknown)));
        zassert_ok(flash_write(
            flash_area_get_device(area), area->fa_off + area->fa_size, outside, sizeof(outside)));
        fail_layout_read = !strcmp(phase, "layout_read_error");
        fail_layout_erase = !strcmp(phase, "layout_erase_error");
        fail_layout_write = !strcmp(phase, "layout_write_error");
        bool failed = fail_layout_read || fail_layout_erase || fail_layout_write;
        zassert_equal(dp_init(), failed ? -EIO : 0);
        if (fail_layout_read || fail_layout_erase) {
            /* 读取失败不能擦除；擦除失败不能继续写布局头。 */
            int erases = layout_erases;
            if (fail_layout_read) {
                zassert_equal(erases, 0);
            }
            fail_layout_read = false;
            fail_layout_erase = false;
            uint8_t actual[32];
            zassert_ok(flash_area_read(area, 0, actual, sizeof(actual)));
            zassert_mem_equal(unknown, actual, sizeof(actual));
        }
        fail_layout_write = false;
        zassert_ok(dp_init());
        uint8_t actual[32];
        zassert_ok(flash_area_read(area, 0, actual, sizeof(actual)));
        zassert_mem_equal(actual, "DPSLOTS", 7);
        zassert_ok(flash_area_read(area, area->fa_size - 32, actual, sizeof(actual)));
        uint8_t erased = flash_get_parameters(flash_area_get_device(area))->erase_value;
        for (size_t i = 0; i < sizeof(actual); ++i) {
            zassert_equal(actual[i], erased);
        }
        zassert_ok(flash_read(
            flash_area_get_device(area), area->fa_off + area->fa_size, actual, sizeof(actual)));
        zassert_mem_equal(actual, outside, sizeof(actual));
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_true(point(101, 0)->saved_valid);
        return;
    }
    if (!strcmp(phase, "map_read_error")) {
        fail_mapping_read = true;
        zassert_equal(dp_init(), -EIO);
        zassert_equal(layout_erases, 0);
        fail_mapping_read = false;
        zassert_ok(dp_init());
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 11);
        return;
    }
    if (!strcmp(phase, "mapping_repeat")) {
        force_invalid_mapping = true;
        zassert_equal(dp_init(), -EBADMSG);
        zassert_equal(layout_erases, 2); /* 初次建区一次，映射重建最多一次。 */
        force_invalid_mapping = false;
        zassert_ok(dp_init());
        return;
    }
    zassert_equal(dp_flush(), -EACCES);
    zassert_ok(dp_init());
    zassert_ok(dp_init());
    if (!strncmp(phase, "corrupt_seed_", 13)) {
        zassert_ok(dp_register_device(101, &table));
        set(101, 77);
        zassert_ok(dp_flush());
        /* 写入 CRC 正确、内容非法的真实 NVS 映射供下一进程验证。 */
        const struct flash_area *area;
        zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_ALIAS(dp_params)), &area));
        struct nvs_fs fs = {.flash_device = flash_area_get_device(area),
            .offset = area->fa_off + 4096,
            .sector_size = 4096,
            .sector_count = 2};
        zassert_ok(nvs_mount(&fs));
        uint8_t record[4] = {1, 1, 101, 0};
        uint16_t id = 1;
        size_t size = sizeof(record);
        if (!strcmp(phase, "corrupt_seed_zero")) {
            record[2] = 0;
        } else if (!strcmp(phase, "corrupt_seed_duplicate")) {
            id = 2;
        } else if (!strcmp(phase, "corrupt_seed_state")) {
            record[1] = 99;
        } else if (!strcmp(phase, "corrupt_seed_version")) {
            record[0] = 99;
        } else if (!strcmp(phase, "corrupt_seed_length")) {
            size = 3;
        } else {
            zassert_unreachable("Unknown mapping corruption phase");
        }
        zassert_equal(nvs_write(&fs, id, record, size), size);
    } else if (!strcmp(phase, "corrupt_recover")) {
        zassert_equal(layout_erases, 1);
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_true(point(101, 0)->saved_valid);
    } else if (!strcmp(phase, "factory") || !strcmp(phase, "factory_null") ||
               !strcmp(phase, "factory_failure")) {

        zassert_ok(dp_register_device(101, &table));
        zassert_ok(dp_register_device(102, &table));
        set(101, 33);
        access_during_erase = true;
        if (!strcmp(phase, "factory_failure")) {
            fail_erase = true;
            zassert_equal(dp_factory_reset(101, before_reboot, &reset_callback_called), -EIO);
            zassert_false(reset_callback_called);
            fail_erase = false;
            const value_t value = {.u16 = 44};
            value_t output = {0};
            zassert_ok(dp_set_para(101, 300, &value));
            zassert_ok(dp_get_para(101, 300, &output));
            zassert_equal(output.u16, 44);
            zassert_ok(dp_restore_defaults(101));
            int writes = point_writes;
            zassert_ok(dp_flush());
            zassert_equal(point_writes, writes);
        }
        reboot_expected = true;
        if (!setjmp(reboot_jump)) {
            bool callback = strcmp(phase, "factory_null") != 0;
            dp_factory_reset(101, callback ? before_reboot : NULL, &reset_callback_called);
            zassert_unreachable("Factory reset must reboot");
        }
        zassert_equal(reset_callback_called, strcmp(phase, "factory_null") != 0);
        zassert_not_null(dp_tables());
        zassert_false(access_during_erase);
        int writes = point_writes;
        zassert_ok(dp_flush());
        zassert_equal(point_writes, writes);
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "factory_unregistered") ||
               !strcmp(phase, "factory_unregistered_failure")) {
        point_read_error = -EIO;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_is_null(point(101, 0));
        point_read_error = 0;
        zassert_ok(dp_register_device(102, &table));
        if (!strcmp(phase, "factory_unregistered_failure")) {
            fail_erase = true;
            zassert_equal(dp_factory_reset(101, before_reboot, &reset_callback_called), -EIO);
            zassert_false(reset_callback_called);
            fail_erase = false;
        }
        reboot_expected = true;
        if (!setjmp(reboot_jump)) {
            dp_factory_reset(101, before_reboot, &reset_callback_called);
            zassert_unreachable("Factory reset must reboot");
        }
        zassert_true(reset_callback_called);
        zassert_is_null(point(101, 0));
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "factory_absent")) {
        reboot_expected = true;
        if (!setjmp(reboot_jump)) {
            dp_factory_reset(999, NULL, NULL);
            zassert_unreachable("Factory reset must reboot even without stored parameters");
        }
        zassert_false(reset_callback_called);
    } else if (!strcmp(phase, "factory_all") || !strcmp(phase, "factory_all_failure")) {
        zassert_ok(dp_register_device(101, &table));
        /* 102 故意不登记，验证总区清理覆盖历史设备。 */
        set(101, 66);
        access_during_erase = true;
        if (!strcmp(phase, "factory_all_failure")) {
            fail_layout_erase = true;
            zassert_equal(
                dp_factory_reset(DP_DEVICE_ID_ALL, before_reboot, &reset_callback_called), -EIO);
            zassert_false(reset_callback_called);
            fail_layout_erase = false;
            set(101, 55);
            zassert_equal(get(101), 55);
            int writes = point_writes;
            zassert_ok(dp_flush());
            zassert_equal(writes, point_writes);
        }
        reboot_expected = true;
        if (!setjmp(reboot_jump)) {
            dp_factory_reset(DP_DEVICE_ID_ALL, before_reboot, &reset_callback_called);
            zassert_unreachable("Factory reset must reboot");
        }
        zassert_true(reset_callback_called);
        zassert_false(access_during_erase);
        int writes = point_writes;
        zassert_ok(dp_flush());
        zassert_equal(writes, point_writes);
    } else if (!strcmp(phase, "factory_all_recover")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(101), 10);
        zassert_equal(get(102), 10);
        zassert_true(point(101, 0)->saved_valid);
        zassert_true(point(102, 0)->saved_valid);
    } else if (!strcmp(phase, "factory_recover")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_true(point(101, 0)->saved_valid);
        zassert_true(point(101, 2)->saved_valid);
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "exercise")) {
        exercise();
    } else if (!strcmp(phase, "seed")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_ok(dp_register_device(102, &table));
        set(101, 11);
        set(102, 22);
        const value_t protected_value = {.words = {0xabcd, 0xef01}};
        zassert_ok(dp_set_para(101, 500, &protected_value));
        const value_t signed_value = {.s32 = -123456};
        zassert_ok(dp_set_para(101, 501, &signed_value));
        zassert_ok(dp_flush());
        set(101, 12); /* 模拟保存周期内掉电，这个值不能跨启动保留。 */
    } else if (!strcmp(phase, "recover")) {
        zassert_ok(dp_register_device(102, &table));
        zassert_ok(dp_register_device(101, &reduced));
        zassert_equal(get(101), 11);
        zassert_true(point(101, 0)->saved_valid);
        zassert_equal(point(101, 0)->saved_value->u16, 11);
        zassert_false(point(101, 0)->dirty);
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "incompatible") || !strcmp(phase, "out_of_range")) {
        dp_point_config_t changed = rows[0];
        if (!strcmp(phase, "incompatible")) {
            changed.type = VALUE_TYPE_U32;
            changed.size = 4;
            changed.data.initial.u32 = 3;
        } else {
            changed.data.initial.u16 = 1;
            changed.data.maximum.u16 = 5;
        }
        const dp_table_t changed_table = {.points = &changed, .point_count = 1};
        zassert_ok(dp_register_device(101, &changed_table));
        value_t output = {0};
        zassert_ok(dp_get_para(101, 300, &output));
        zassert_equal(output.u32, !strcmp(phase, "incompatible") ? 3 : 1);
        zassert_true(point(101, 0)->saved_valid);
        zassert_false(point(101, 0)->dirty);
        zassert_equal(point_writes, 1);
        /* 旧值已被新初值替换；其他设备的历史参数保持不变。 */
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "incompatible_recover")) {
        dp_point_config_t changed = rows[0];
        changed.type = VALUE_TYPE_U32;
        changed.size = 4;
        changed.data.initial.u32 = 4;
        const dp_table_t changed_table = {.points = &changed, .point_count = 1};
        zassert_ok(dp_register_device(101, &changed_table));
        value_t output = {0};
        zassert_ok(dp_get_para(101, 300, &output));
        zassert_equal(output.u32, 3);
        zassert_equal(point_writes, 0);
    } else if (!strcmp(phase, "range_recover")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 1);
        zassert_equal(point_writes, 0);
    } else if (!strcmp(phase, "point_read_error")) {
        for (int i = 0; i < 2; ++i) {
            point_read_error = i ? -EBADMSG : -EIO;
            int writes = point_writes;
            zassert_equal(dp_register_device(101, &table), point_read_error);
            zassert_is_null(point(101, 0));
            zassert_equal(point_writes, writes);
        }
        point_read_error = 0;
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 11);
    } else if (!strcmp(phase, "initial_write_failure")) {
        fail_write = true;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_is_null(point(101, 0));
        fail_write = false;
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_true(point(101, 0)->saved_valid);
    } else if (!strcmp(phase, "initial_seed")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(point_writes, 3);
        zassert_true(point(101, 0)->saved_valid);
        /* 不调用 flush，下一进程仍应读到登记时追加的初值。 */
    } else if (!strcmp(phase, "initial_recover")) {
        dp_point_config_t changed = rows[0];
        changed.data.initial.u16 = 20;
        const dp_table_t changed_table = {.points = &changed, .point_count = 1};
        zassert_ok(dp_register_device(101, &changed_table));
        zassert_equal(get(101), 10);
        zassert_equal(point_writes, 0);
    } else if (!strcmp(phase, "fallback_write_failure")) {
        dp_point_config_t changed = rows[0];
        changed.data.initial.u16 = 1;
        changed.data.maximum.u16 = 5;
        const dp_table_t changed_table = {.points = &changed, .point_count = 1};
        fail_write = true;
        zassert_equal(dp_register_device(101, &changed_table), -EIO);
        zassert_is_null(point(101, 0));
        fail_write = false;
        /* 回退写入失败没有发布点表；原历史值仍可被兼容配置读回。 */
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 11);
    } else if (!strcmp(phase, "clear_unregistered")) {
        point_read_error = -EIO;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_is_null(point(101, 0));
        /* 无 RAM 设备也能主动删除历史 Flash，其他设备保持不变。 */
        zassert_ok(dp_remove_device(101));
        point_read_error = 0;
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(102), 22);
    } else if (!strcmp(phase, "write_uncertain")) {
        zassert_ok(dp_register_device(101, &table));
        set(101, 22);
        commit_point_then_fail = true;
        zassert_equal(dp_flush(), -EIO);
        commit_point_then_fail = false;
        zassert_false(point(101, 0)->saved_valid);
        set(101, 11); /* 回到旧副本也不能取消重试。 */
        zassert_true(point(101, 0)->dirty);
        zassert_ok(dp_flush());
        zassert_true(point(101, 0)->saved_valid);
        zassert_false(point(101, 0)->dirty);
    } else if (!strcmp(phase, "defaults_uncertain")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_ok(dp_restore_defaults(101));
        zassert_ok(dp_flush());
        set(101, 22);
        commit_point_then_fail = true;
        zassert_equal(dp_flush(), -EIO);
        commit_point_then_fail = false;
        zassert_false(point(101, 0)->saved_valid);
        zassert_ok(dp_restore_defaults(101));
        zassert_true(point(101, 0)->dirty);
        zassert_ok(dp_flush());
        zassert_true(point(101, 0)->saved_valid);
    } else if (!strcmp(phase, "defaults_uncertain_recover")) {
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_equal(point_writes, 0);
    } else if (!strcmp(phase, "delete") || !strcmp(phase, "delete_metadata")) {
        zassert_ok(dp_register_device(101, &table));
        value_t signed_value;
        zassert_ok(dp_get_para(101, 501, &signed_value));
        zassert_equal(signed_value.s32, -123456); /* 暂时删点不删除历史。 */
        fail_erase = !strcmp(phase, "delete");
        fail_delete = !strcmp(phase, "delete_metadata");
        zassert_equal(dp_remove_device(101), -EIO);
        zassert_not_null(point(101, 0));
        zassert_equal(dp_register_device(101, &table), -EEXIST);
        int writes = point_writes;
        set(101, 42);
        zassert_equal(get(101), 42);
        zassert_ok(dp_flush());
        zassert_equal(writes, point_writes);
    } else if (!strcmp(phase, "recover_delete")) {
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(102), 22);
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_ok(dp_remove_device(101));
        zassert_ok(dp_remove_device(102));
        for (uint16_t id = 200; id < 200 + CONFIG_DP_STORAGE_SLOT_COUNT; ++id) {
            zassert_ok(dp_register_device(id, &table));
        }
        zassert_equal(dp_register_device(200 + CONFIG_DP_STORAGE_SLOT_COUNT, &table), -ENOSPC);
        zassert_ok(dp_remove_device(202));
        zassert_ok(dp_register_device(200 + CONFIG_DP_STORAGE_SLOT_COUNT, &table));
    } else if (!strcmp(phase, "map_failure")) {
        fail_map = true;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_is_null(point(101, 0));
        fail_map = false;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_equal(dp_remove_device(101), -EIO);
        zassert_ok(dp_register_device(102, &table));
        set(102, 22);
        zassert_ok(dp_flush());
        fail_map = true;
        zassert_equal(dp_remove_device(102), -EIO);
        fail_map = false;
        zassert_equal(get(102), 22);
        zassert_ok(dp_register_device(103, &table));
        zassert_ok(dp_remove_device(103));
        set(102, 23);
        zassert_ok(dp_flush());
        zassert_equal(dp_remove_device(102), -EIO);
    } else if (!strcmp(phase, "map_commit")) {
        /* 模拟已经持久提交，但底层仍向上报告写失败。 */
        commit_map_then_fail = true;
        zassert_equal(dp_register_device(101, &table), -EIO);
        commit_map_then_fail = false;
        zassert_equal(dp_register_device(101, &table), -EIO);
        zassert_ok(dp_register_device(102, &table));
        set(102, 22);
        zassert_ok(dp_flush());
        commit_map_then_fail = true;
        zassert_equal(dp_remove_device(102), -EIO);
        commit_map_then_fail = false;
        zassert_equal(get(102), 22);
        zassert_ok(dp_register_device(103, &table));
        set(103, 33);
        zassert_ok(dp_flush());
    } else if (!strcmp(phase, "map_commit_recover")) {
        zassert_ok(dp_register_device(103, &table));
        zassert_equal(get(103), 33);
        zassert_ok(dp_register_device(101, &table));
        zassert_equal(get(101), 10);
        zassert_ok(dp_register_device(102, &table));
        zassert_equal(get(102), 10); /* 上次删除标记已落盘，启动完成清理。 */
    } else if (!strcmp(phase, "periodic")) {
        zassert_ok(dp_register_device(101, &table));
        set(101, 44);
        k_sleep(K_MSEC(CONFIG_DP_SAVE_INTERVAL_MS * 2 + 10));
        zassert_false(point(101, 0)->dirty);
        zassert_true(point_writes > 0);
    } else {
        zassert_unreachable("Unknown test phase");
    }
}

ZTEST_SUITE(dp_storage, NULL, NULL, NULL, NULL, NULL);
