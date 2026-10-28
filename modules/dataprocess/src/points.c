#include <dataprocess/dataprocess.h>
#include "dp_internal.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/hash_map.h>

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
static K_MUTEX_DEFINE(store_lock);
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

static int find_point(uint16_t device_id,
    uint16_t signal_key,
    const dp_point_config_t **point,
    dp_point_data_t **state,
    size_t *signal_index)
{
    if (!device_id || !signal_key) {
        return -EINVAL;
    }
    dp_registered_table_t *table = find_table(device_id);
    if (!table) {
        return -ENOENT;
    }
    size_t i;
    for (i = 0; i < table->point_count; ++i) {
        if (table->points[i].signal_key == signal_key) {
            break;
        }
    }
    if (i == table->point_count) {
        return -ENOENT;
    }
    *signal_index = i;
    *state = &table->data[i];
    *point = &table->points[i];
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
    k_mutex_lock(&store_lock, K_FOREVER);
    stored->value = *value;
    stored->updated_at_s = dp_timestamp_s();
    k_mutex_unlock(&store_lock);
    return 0;
}

/* ==================== 对外接口 ==================== */

int dp_register_table(uint16_t device_id, const dp_table_t *table)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!device_id || !table || !table->points || !table->point_count) {
        return -EINVAL;
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

    dp_registered_table_t *entry =
        k_calloc(1, sizeof(*entry) + table->point_count * sizeof(entry->data[0]));
    if (!entry) {
        return -ENOMEM;
    }
    entry->device_id = device_id;
    entry->points = table->points;
    entry->point_count = table->point_count;
    for (size_t i = 0; i < table->point_count; ++i) {
        const dp_point_config_t *point = &table->points[i];
        err = validate_point(device_id, point);
        if (!err) {
            entry->data[i].value = point->data.initial;
            err = check_value(point, &entry->data[i].value);
        }
        if (err) {
            err = -EINVAL;
            goto failed;
        }
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

int dp_set_para(uint16_t device_id, uint16_t signal_key, const value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    const dp_point_config_t *p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    value_t pending = *value;
    if (p->write_callback) {
        const dp_callback_context_t context = {
            .device_id = device_id,
            .signal_key = signal_key,
            .signal_index = signal_index,
            .is_write = true,
            .value = &pending,
        };
        err = p->write_callback(&context);
        if (err) {
            return err;
        }
    }
    return save_value(p, stored, &pending);
}

int dp_get_para(uint16_t device_id, uint16_t signal_key, value_t *value)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!value) {
        return -EINVAL;
    }
    const dp_point_config_t *p;
    dp_point_data_t *stored;
    size_t signal_index;
    int err = find_point(device_id, signal_key, &p, &stored, &signal_index);
    if (err) {
        return err;
    }
    dp_point_data_t state;
    k_mutex_lock(&store_lock, K_FOREVER);
    state = *stored;
    k_mutex_unlock(&store_lock);
    if (p->read_callback) {
        const dp_callback_context_t context = {
            .device_id = device_id,
            .signal_key = signal_key,
            .signal_index = signal_index,
            .is_write = false,
            .value = &state.value,
        };
        err = p->read_callback(&context);
        if (err) {
            return err;
        }
        err = save_value(p, stored, &state.value);
        if (err) {
            return err;
        }
    }
    *value = state.value;
    return 0;
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
    k_mutex_lock(&store_lock, K_FOREVER);
    for (size_t i = 0; i < table->point_count; ++i) {
        if (table->points[i].flags & DP_POINT_FLAG_SKIP_RESTORE) {
            continue;
        }
        table->data[i].value = table->points[i].data.initial;
        table->data[i].updated_at_s = restored_at_s;
    }
    k_mutex_unlock(&store_lock);
    return 0;
}
