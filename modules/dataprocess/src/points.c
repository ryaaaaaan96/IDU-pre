#include <dataprocess/dataprocess.h>
#include "dp_internal.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/hash_map.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(dp, LOG_LEVEL_INF);

/* 单次分配中不会使节点头及点数据总长度溢出的最大点数。 */
#define DP_MAX_ALLOCATABLE_POINTS                                                                  \
    ((SIZE_MAX - sizeof(dp_registered_table_t)) / sizeof(dp_point_data_t))

/* 保留成员自身的有符号性，switch 选择成员后直接比较配置上下限。 */
#define DP_OUTSIDE_RANGE(point, value, member)                                                     \
    ((value)->member < (point)->data.minimum.member ||                                             \
        (value)->member > (point)->data.maximum.member)

/* 应用只提供只读点表，设备目录与点数据均由模块私有持有。 */
static dp_registered_table_t *tables;
static K_MUTEX_DEFINE(dp_values_lock);
static K_MUTEX_DEFINE(dp_devices_lock);
static bool initialized;
/* 启动登记串行执行；检查结束后释放全部桶，不保留运行期查找索引。 */
SYS_HASHMAP_OA_LP_DEFINE_STATIC_ADVANCED(registration_keys,
    sys_hash32,
    k_realloc,
    SYS_HASHMAP_CONFIG(SIZE_MAX, SYS_HASHMAP_DEFAULT_LOAD_FACTOR));

#ifdef CONFIG_DP_HASH_LOOKUP
SYS_HASHMAP_OA_LP_DEFINE_STATIC_ADVANCED(device_lookup,
    sys_hash32,
    k_realloc,
    SYS_HASHMAP_CONFIG(SIZE_MAX, SYS_HASHMAP_DEFAULT_LOAD_FACTOR));
#endif

/* ==================== 内部实现 ==================== */

/* 类型合法性先于业务回调检查；数组类仅允许同类型访问。 */
static bool can_convert(value_type_t source, value_type_t target)
{
    switch (source) {
    case VALUE_TYPE_U8:
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        switch (target) {
        case VALUE_TYPE_U8:
        case VALUE_TYPE_U16:
        case VALUE_TYPE_S16:
        case VALUE_TYPE_U32:
        case VALUE_TYPE_S32:
            return true;
        default:
            return false;
        }
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_BYTES:
    case VALUE_TYPE_WORDS:
        return source == target;
    default:
        return false;
    }
}

/* 调用方已检查类型组合；中间值覆盖现有全部整数类型。 */
static void convert_value(
    value_type_t source_type, value_type_t target_type, const value_t *source, value_t *target)
{
    if (source_type == target_type) {
        *target = *source;
        return;
    }
    int64_t number;
    switch (source_type) {
    case VALUE_TYPE_U8:
        number = source->u8;
        break;
    case VALUE_TYPE_U16:
        number = source->u16;
        break;
    case VALUE_TYPE_S16:
        number = source->s16;
        break;
    case VALUE_TYPE_U32:
        number = source->u32;
        break;
    default:
        number = source->s32;
        break;
    }
    *target = (value_t){0};
    switch (target_type) {
    case VALUE_TYPE_U8:
        target->u8 = (uint8_t)number;
        break;
    case VALUE_TYPE_U16:
        target->u16 = (uint16_t)number;
        break;
    case VALUE_TYPE_S16: {
        uint16_t bits = (uint16_t)number;
        int32_t signed_number = bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536;
        target->s16 = (int16_t)signed_number;
        break;
    }
    case VALUE_TYPE_U32:
        target->u32 = (uint32_t)number;
        break;
    default: {
        uint32_t bits = (uint32_t)number;
        int64_t signed_number = bits <= INT32_MAX ? (int64_t)bits : (int64_t)bits - 4294967296LL;
        target->s32 = (int32_t)signed_number;
        break;
    }
    }
}

static int check_value(const dp_point_config_t *point, const value_t *value)
{
    if (point->type == VALUE_TYPE_STRING || point->type == VALUE_TYPE_BYTES ||
        point->type == VALUE_TYPE_WORDS) {
        return 0;
    }
    if (!(point->flags & DP_POINT_FLAG_CHECK_RANGE)) {
        return 0;
    }
    bool outside;
    switch (point->type) {
    case VALUE_TYPE_U8:
        outside = DP_OUTSIDE_RANGE(point, value, u8);
        break;
    case VALUE_TYPE_U16:
        outside = DP_OUTSIDE_RANGE(point, value, u16);
        break;
    case VALUE_TYPE_S16:
        outside = DP_OUTSIDE_RANGE(point, value, s16);
        break;
    case VALUE_TYPE_U32:
        outside = DP_OUTSIDE_RANGE(point, value, u32);
        break;
    case VALUE_TYPE_S32:
        outside = DP_OUTSIDE_RANGE(point, value, s32);
        break;
    default:
        return -ENOTSUP;
    }
    return outside ? -ERANGE : 0;
}

/* 登记时校验一次；运行期间配置常驻只读，不再解析或复制配置行。 */
static int validate_point(uint16_t device_id, const dp_point_config_t *point)
{
    if (!point->signal_key || !point->size || point->size > sizeof(point->data.initial.bytes)) {
        return -EINVAL;
    }
    if ((point->flags & DP_POINT_FLAG_PERSISTENT) && point->signal_key == UINT16_MAX) {
        return -EINVAL; /* NVS 的 0xffff 为内部保留 ID。 */
    }
    switch (point->type) {
    case VALUE_TYPE_U8:
        if (point->size != 1) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_U16:
    case VALUE_TYPE_S16:
        if (point->size != 2) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_U32:
    case VALUE_TYPE_S32:
        if (point->size != 4) {
            return -EINVAL;
        }
        break;
    case VALUE_TYPE_WORDS:
        if (point->size & 1U) {
            return -EINVAL;
        }
        __fallthrough;
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_BYTES:
        if (point->flags & DP_POINT_FLAG_CHECK_RANGE) {
            LOG_WRN("Device %u key %u: range check ignored for nonnumeric point",
                device_id,
                point->signal_key);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

dp_registered_table_t *dp_tables(void)
{
    return tables;
}

static dp_registered_table_t *find_table(uint16_t device_id)
{
#ifdef CONFIG_DP_HASH_LOOKUP
    uint64_t value;
    if (sys_hashmap_get(&device_lookup, device_id, &value)) {
        return (dp_registered_table_t *)(uintptr_t)value;
    }
#else
    for (dp_registered_table_t *table = tables; table; table = table->next) {
        if (table->device_id == device_id) {
            return table;
        }
    }
#endif
    return NULL;
}

/* 仅描述本次调用已定位的行，不保存寻址方式或增加常驻索引。 */
typedef struct {
    dp_registered_table_t *table;
    size_t index;
} dp_point_ref_t;

static int find_point_by_key(uint16_t device_id, uint16_t signal_key, dp_point_ref_t *point)
{
    if (!device_id || !signal_key) {
        return -EINVAL;
    }
    dp_registered_table_t *table = find_table(device_id);
    if (!table) {
        return -ENOENT;
    }
    for (size_t i = 0; i < table->point_count; ++i) {
        if (table->points[i].signal_key == signal_key) {
            point->table = table;
            point->index = i;
            return 0;
        }
    }
    return -ENOENT;
}

static int find_point_by_index(uint16_t device_id, size_t signal_index, dp_point_ref_t *point)
{
    if (!device_id) {
        return -EINVAL;
    }
    dp_registered_table_t *table = find_table(device_id);
    if (!table || signal_index >= table->point_count) {
        return -ENOENT;
    }
    point->table = table;
    point->index = signal_index;
    return 0;
}

/* 将 hashmap 仅用作 key 集合；插入已有 key 返回 0。 */
static int check_unique_keys(const dp_table_t *table)
{
    int err = 0;
    for (size_t i = 0; i < table->point_count; ++i) {
        int result = sys_hashmap_insert(&registration_keys, table->points[i].signal_key, 0, NULL);
        if (result <= 0) {
            err = result < 0 ? result : -EEXIST;
            break;
        }
    }
    sys_hashmap_clear(&registration_keys, NULL, NULL);
    return err;
}

/* 回调只操作副本；最终校验及存储成功后才更新点数据。 */
static int save_value(const dp_point_config_t *point, dp_point_data_t *stored, const value_t *value)
{
    int err = check_value(point, value);
    if (err) {
        return err;
    }
    uint32_t updated_at_s = dp_timestamp_s();
    k_mutex_lock(&dp_values_lock, K_FOREVER);
    if (stored->saved_value) {
        if (stored->saved_valid) {
            stored->dirty = memcmp(stored->saved_value->bytes, value->bytes, point->size) != 0;
        } else if (memcmp(stored->value.bytes, value->bytes, point->size)) {
            stored->dirty = true;
        }
    }
    stored->value = *value;
    stored->updated_at_s = updated_at_s;
    k_mutex_unlock(&dp_values_lock);
    return 0;
}

static int register_device(uint16_t device_id, const dp_table_t *table)
{
    if (!device_id || !table || !table->points || !table->point_count) {
        return -EINVAL;
    }
    if (!initialized) {
        return -EACCES;
    }
    if (find_table(device_id)) {
        return -EEXIST;
    }
    if (table->point_count > DP_MAX_ALLOCATABLE_POINTS) {
        return -EOVERFLOW;
    }

    int err = check_unique_keys(table);
    if (err) {
        return err;
    }

    size_t persistent_count = 0;
    for (size_t i = 0; i < table->point_count; ++i) {
        const dp_point_config_t *point = &table->points[i];
        err = validate_point(device_id, point);
        if (err) {
            return -EINVAL;
        }
        err = check_value(point, &point->data.initial);
        if (err) {
            return -EINVAL;
        }
        if (point->flags & DP_POINT_FLAG_PERSISTENT) {
            ++persistent_count;
        }
    }
    size_t allocation_size =
        sizeof(dp_registered_table_t) + table->point_count * sizeof(dp_point_data_t);
    if (persistent_count > (SIZE_MAX - allocation_size) / sizeof(value_t)) {
        return -EOVERFLOW;
    }
    allocation_size += persistent_count * sizeof(value_t);
    dp_registered_table_t *entry = k_calloc(1, allocation_size);
    if (!entry) {
        return -ENOMEM;
    }
    entry->device_id = device_id;
    entry->points = table->points;
    entry->point_count = table->point_count;
    if (persistent_count) {
        err = dp_params_attach(entry);
        if (err) {
            goto failed;
        }
    }
    value_t *saved = (value_t *)&entry->data[table->point_count];
    for (size_t i = 0; i < table->point_count; ++i) {
        const dp_point_config_t *point = &table->points[i];
        entry->data[i].value = point->data.initial;
        if (!(point->flags & DP_POINT_FLAG_PERSISTENT)) {
            continue;
        }
        entry->data[i].saved_value = saved++;
        err = dp_params_load(entry, i);
        if (err && err != -ENOENT && err != -ESTALE) {
            goto failed;
        }
        if (!err) {
            err = check_value(point, &entry->data[i].value);
        }
        if (err) {
            if (err != -ENOENT) {
                LOG_WRN("Device %u key %u: stored value rejected (%d), using initial value",
                    device_id,
                    point->signal_key,
                    err);
            }
            entry->data[i].value = point->data.initial;
            err = dp_params_write(entry, i, &entry->data[i].value);
            if (err) {
                goto failed;
            }
        }
        *entry->data[i].saved_value = entry->data[i].value;
        entry->data[i].saved_valid = true;
    }
#ifdef CONFIG_DP_HASH_LOOKUP
    err = sys_hashmap_insert(&device_lookup, device_id, (uint64_t)(uintptr_t)entry, NULL);
    if (err < 0) {
        goto failed;
    }
#endif
    /* 全部准备完成才发布；设备链表无需维护登记顺序。 */
    entry->next = tables;
    tables = entry;
    return 0;
failed:
    k_free(entry);
    return err;
}

static int set_para(const dp_point_ref_t *point, value_type_t input_type, const value_t *value)
{
    const dp_point_config_t *p = &point->table->points[point->index];
    dp_point_data_t *stored = &point->table->data[point->index];
    if (!can_convert(input_type, p->type)) {
        return -ENOTSUP;
    }
    value_t pending;
    convert_value(input_type, p->type, value, &pending);
    if (p->write_callback) {
        const dp_callback_context_t context = {
            .device_id = point->table->device_id,
            .signal_key = p->signal_key,
            .signal_index = point->index,
            .is_write = true,
            .value = &pending,
        };
        int err = p->write_callback(&context);
        if (err) {
            return err;
        }
    }
    return save_value(p, stored, &pending);
}

static int get_para(const dp_point_ref_t *point, value_type_t output_type, value_t *value)
{
    const dp_point_config_t *p = &point->table->points[point->index];
    dp_point_data_t *stored = &point->table->data[point->index];
    if (!can_convert(p->type, output_type)) {
        return -ENOTSUP;
    }
    value_t pending;
    k_mutex_lock(&dp_values_lock, K_FOREVER);
    pending = stored->value;
    k_mutex_unlock(&dp_values_lock);
    if (p->read_callback) {
        const dp_callback_context_t context = {
            .device_id = point->table->device_id,
            .signal_key = p->signal_key,
            .signal_index = point->index,
            .is_write = false,
            .value = &pending,
        };
        int err = p->read_callback(&context);
        if (err) {
            return err;
        }
        err = save_value(p, stored, &pending);
        if (err) {
            return err;
        }
    }
    convert_value(p->type, output_type, &pending, value);
    return 0;
}

/* ==================== 对外接口 ==================== */

int dp_init(void)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&dp_devices_lock, K_FOREVER);
    int err = 0;
    if (!initialized) {
        err = dp_params_init();
        if (!err) {
            initialized = true;
        }
    }
    k_mutex_unlock(&dp_devices_lock);
    return err;
}

int dp_register_device(uint16_t device_id, const dp_table_t *table)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&dp_devices_lock, K_FOREVER);
    int err = register_device(device_id, table);
    k_mutex_unlock(&dp_devices_lock);
    return err;
}

int dp_flush(void)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&dp_devices_lock, K_FOREVER);
    int result = initialized ? 0 : -EACCES;
    for (dp_registered_table_t *device = initialized ? tables : NULL; device;
        device = device->next) {
        if (device->factory_resetting) {
            continue;
        }
        for (size_t i = 0; i < device->point_count; ++i) {
            /* 配置常驻只读；非持久点不需要检查 dirty，也不获取点值锁。 */
            if (!(device->points[i].flags & DP_POINT_FLAG_PERSISTENT)) {
                continue;
            }
            k_mutex_lock(&dp_values_lock, K_FOREVER);
            dp_point_data_t snapshot = device->data[i];
            k_mutex_unlock(&dp_values_lock);
            if (!snapshot.dirty) {
                continue;
            }
            int err = dp_params_write(device, i, &snapshot.value);
            k_mutex_lock(&dp_values_lock, K_FOREVER);
            dp_point_data_t *stored = &device->data[i];
            if (err) {
                /* 写入结果不确定，旧副本不能再用于取消后续保存。 */
                stored->saved_valid = false;
                stored->dirty = true;
                if (!result) {
                    result = err;
                }
            } else {
                *stored->saved_value = snapshot.value;
                stored->saved_valid = true;
                stored->dirty =
                    memcmp(stored->value.bytes, snapshot.value.bytes, device->points[i].size) != 0;
            }
            k_mutex_unlock(&dp_values_lock);
        }
    }
    k_mutex_unlock(&dp_devices_lock);
    return result;
}

int dp_remove_device(uint16_t device_id)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!device_id) {
        return -EINVAL;
    }
    k_mutex_lock(&dp_devices_lock, K_FOREVER);
    dp_registered_table_t *device = find_table(device_id);
    if (device) {
        /* 删除失败也暂停保存，避免旧值写回已删除一部分的槽位。 */
        device->factory_resetting = true;
    }
    int err = initialized ? dp_params_remove(device_id) : -EACCES;
    if (err) {
        goto done;
    }
    dp_registered_table_t **link = &tables;
    while (*link && (*link)->device_id != device_id) {
        link = &(*link)->next;
    }
    if (*link) {
        dp_registered_table_t *entry = *link;
#ifdef CONFIG_DP_HASH_LOOKUP
        sys_hashmap_remove(&device_lookup, device_id, NULL);
#endif
        *link = entry->next;
        k_free(entry);
    }
done:
    k_mutex_unlock(&dp_devices_lock);
    return err;
}

int dp_set_para_by_key(
    uint16_t device_id, uint16_t signal_key, value_type_t input_type, const value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    dp_point_ref_t point;
    int err = find_point_by_key(device_id, signal_key, &point);
    if (err) {
        return err;
    }
    return set_para(&point, input_type, value);
}

int dp_get_para_by_key(
    uint16_t device_id, uint16_t signal_key, value_type_t output_type, value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    dp_point_ref_t point;
    int err = find_point_by_key(device_id, signal_key, &point);
    if (err) {
        return err;
    }
    return get_para(&point, output_type, value);
}

int dp_set_para_by_index(
    uint16_t device_id, size_t signal_index, value_type_t input_type, const value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    dp_point_ref_t point;
    int err = find_point_by_index(device_id, signal_index, &point);
    if (err) {
        return err;
    }
    return set_para(&point, input_type, value);
}

int dp_get_para_by_index(
    uint16_t device_id, size_t signal_index, value_type_t output_type, value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    dp_point_ref_t point;
    int err = find_point_by_index(device_id, signal_index, &point);
    if (err) {
        return err;
    }
    return get_para(&point, output_type, value);
}

int dp_restore_defaults(uint16_t device_id)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!device_id) {
        return -EINVAL;
    }
    dp_registered_table_t *table = find_table(device_id);
    if (!table) {
        return -ENOENT;
    }
    uint32_t restored_at_s = dp_timestamp_s();
    k_mutex_lock(&dp_values_lock, K_FOREVER);
    for (size_t i = 0; i < table->point_count; ++i) {
        if (table->points[i].flags & DP_POINT_FLAG_SKIP_DEFAULTS) {
            continue;
        }
        dp_point_data_t *stored = &table->data[i];
        const dp_point_config_t *point = &table->points[i];
        if (stored->saved_value) {
            if (stored->saved_valid) {
                stored->dirty =
                    memcmp(stored->saved_value->bytes, point->data.initial.bytes, point->size) != 0;
            } else if (memcmp(stored->value.bytes, point->data.initial.bytes, point->size)) {
                stored->dirty = true;
            }
        }
        table->data[i].value = table->points[i].data.initial;
        table->data[i].updated_at_s = restored_at_s;
    }
    k_mutex_unlock(&dp_values_lock);
    return 0;
}

int dp_factory_reset(uint16_t device_id, dp_factory_reset_callback_t before_reboot, void *context)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    k_mutex_lock(&dp_devices_lock, K_FOREVER);
    int err = -EACCES;
    if (!initialized) {
        goto done;
    }
    if (device_id == DP_DEVICE_ID_ALL) {
        /* 包含未登记设备的历史记录；先停全部保存，再清理总参数区。 */
        for (dp_registered_table_t *entry = tables; entry; entry = entry->next) {
            entry->factory_resetting = true;
        }
        err = dp_params_remove(device_id);
        goto done;
    }
    dp_registered_table_t *device = find_table(device_id);
    if (device) {
        /* 即使 Flash 操作失败也不恢复保存，避免旧值写回正在清理的槽位。 */
        device->factory_resetting = true;
    }
    err = dp_params_remove(device_id);
done:
    k_mutex_unlock(&dp_devices_lock);
    if (err) {
        return err;
    }
    if (before_reboot) {
        before_reboot(context);
    }
    sys_reboot(SYS_REBOOT_COLD);
}
