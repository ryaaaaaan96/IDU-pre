#ifndef DP_TABLE_REGISTRY_H
#define DP_TABLE_REGISTRY_H

#include <dataprocess/reference_tables.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>
#include <stddef.h>

/* 未使用的设备属性与表槽；实际采集端口和地址由应用配置。 */
#define DP_DEVICE_DEFAULT_PORT 0U
#define DP_DEVICE_DEFAULT_ADDRESS 0U
#define DP_DEVICE_DEFAULT_CODE 0U
#define DP_BINDING_UNUSED_ID 0U
#define DP_BINDING_UNUSED_TYPE PRO_DEFAULT

struct dp_registered_table {
    uint16_t id;
    uint16_t type;
    const void *rows;
    size_t count;
};

struct dp_table_binding {
    uint16_t id;
    uint16_t type;
};

struct dp_device_binding {
    uint8_t dev_id;
    uint8_t port;
    uint8_t coll_addr;
    uint16_t dev_code;
    struct dp_table_binding tables[MAX_PTL_TBL_NUM];
};

/* 启动期装配；失败不修改 output。output 至少包含 count 个元素，
 * 成功后须保持有效且不可修改，直至所有点表服务停止使用。
 * 表内容和登记项不复制到 RAM，只生成 DEV_TABLE_S 指针关联视图。 */
int dp_table_bind(const struct dp_device_binding *devices, size_t count,
                  const struct dp_registered_table *tables, size_t table_count,
                  DEV_TABLE_S *output);
int dp_table_registry_bind(const struct dp_device_binding *devices, size_t count,
                           DEV_TABLE_S *output);
/* Compact product declaration; table rows stay in ROM. Zero collect_table
 * means no acquisition target; collect_port/address are then ignored. */
#define DP_DATA_DEFAULT_COLLECT_TABLE 0U
struct data_device_cfg {
    uint8_t dev_id;
    uint16_t process_table;
    uint16_t collect_table;
    uint8_t collect_port;
    uint8_t collect_addr;
};
int dp_data_device_bind(const struct data_device_cfg *devices, size_t count,
                         DEV_TABLE_S *output);
/* Returns a permanent registry entry; rejects duplicate IDs and wrong types. */
int dp_table_registry_find(uint16_t id, uint16_t type,
                           const struct dp_registered_table **table);
#endif
