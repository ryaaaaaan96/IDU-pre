#include <dataprocess/table_registry.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

static const struct dp_registered_table *find_table(
    const struct dp_registered_table *tables, size_t count, uint16_t id)
{
    for (size_t i = 0; i < count; ++i) {
        if (tables[i].id == id) { return &tables[i]; }
    }
    return NULL;
}

static int validate_tables(const struct dp_registered_table *tables, size_t table_count)
{
    if (!tables || !table_count) { return -EINVAL; }
    for (size_t i = 0; i < table_count; ++i) {
        const struct dp_registered_table *t = &tables[i];
        if (!t->id || !t->rows || !t->count || t->count > UINT16_MAX ||
            (t->type != PRO_DATA_PROCESS && t->type != PRO_MB_MASTER &&
             t->type != PRO_MB_SLAVE && t->type != PRO_UPP_MASTER &&
             t->type != PRO_ADDR_SLAVE)) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            if (tables[j].id == t->id) { return -EEXIST; }
        }
    }
    return 0;
}

int dp_table_bind(const struct dp_device_binding *devices, size_t count,
                  const struct dp_registered_table *tables, size_t table_count,
                  DEV_TABLE_S *output)
{
    if (!devices || !count || !tables || !table_count || !output) { return -EINVAL; }
    int err = validate_tables(tables, table_count);
    if (err) { return err; }
    for (size_t i = 0; i < count; ++i) {
        const struct dp_device_binding *d = &devices[i];
        if (!d->dev_id || !d->tables[0].id ||
            d->tables[0].type != PRO_DATA_PROCESS) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            if (devices[j].dev_id == d->dev_id) { return -EEXIST; }
        }
        for (size_t n = 0; n < MAX_PTL_TBL_NUM; ++n) {
            const struct dp_table_binding *b = &d->tables[n];
            if (!b->id) {
                if (b->type) { return -EINVAL; }
                continue;
            }
            const struct dp_registered_table *t = find_table(tables, table_count, b->id);
            if (!t) { return -ENOENT; }
            if (t->type != b->type || (n && t->type == PRO_DATA_PROCESS)) { return -EINVAL; }
            for (size_t j = 0; j < n; ++j) {
                if (d->tables[j].id == b->id) { return -EEXIST; }
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const struct dp_device_binding *d = &devices[i];
        output[i] = (DEV_TABLE_S){.dev_id = d->dev_id, .port = d->port,
                                  .coll_addr = d->coll_addr, .dev_code = d->dev_code};
        for (size_t n = 0; n < MAX_PTL_TBL_NUM; ++n) {
            if (!d->tables[n].id) { continue; }
            const struct dp_registered_table *t = find_table(tables, table_count, d->tables[n].id);
            output[i].ptl_tbl_cfg[n] = (DEV_TBL_CFG_S){.proto_type = t->type,
                .sig_num = t->count, .pst_table = t->rows};
        }
    }
    return 0;
}

int dp_table_registry_bind(const struct dp_device_binding *devices, size_t count,
                           DEV_TABLE_S *output)
{
    STRUCT_SECTION_START_EXTERN(dp_registered_table);
    size_t table_count;
    STRUCT_SECTION_COUNT(dp_registered_table, &table_count);
    return dp_table_bind(devices, count, STRUCT_SECTION_START(dp_registered_table),
                         table_count, output);
}

int dp_table_registry_find(uint16_t id, uint16_t type,
                           const struct dp_registered_table **table)
{
    if (!id || !table) { return -EINVAL; }
    *table = NULL;
    STRUCT_SECTION_START_EXTERN(dp_registered_table);
    size_t count;
    STRUCT_SECTION_COUNT(dp_registered_table, &count);
    const struct dp_registered_table *tables = STRUCT_SECTION_START(dp_registered_table);
    int err = validate_tables(tables, count);
    if (err) { return err; }
    *table = find_table(tables, count, id);
    if (!*table) { return -ENOENT; }
    return (*table)->type == type ? 0 : -EINVAL;
}

int dp_data_device_bind(const struct data_device_cfg *devices, size_t count,
                         DEV_TABLE_S *output)
{
    if (!devices || !count || !output) { return -EINVAL; }
    /* 发布任何运行视图前，先校验整个声明。 */
    for (size_t i = 0; i < count; ++i) {
        const struct data_device_cfg *d = &devices[i];
        const struct dp_registered_table *process, *collect;
        if (!d->dev_id) { return -EINVAL; }
        int err = dp_table_registry_find(d->process_table, PRO_DATA_PROCESS, &process);
        if (err) { return err; }
        for (size_t j = 0; j < i; ++j) {
            if (devices[j].dev_id == d->dev_id) { return -EEXIST; }
        }
        if (!d->collect_table) { continue; }
        if (!d->collect_addr || d->collect_addr > 247) { return -EINVAL; }
#if defined(CONFIG_COLLECT)
        if (d->collect_port >= CONFIG_CO_MAX_BUSES) { return -EINVAL; }
#endif
        err = dp_table_registry_find(d->collect_table, PRO_MB_MASTER, &collect);
        if (err) { return err; }
        const MODBUS_RTU_TABLE_S *rows = collect->rows;
        const DM_SIG_CFG_S *signals = process->rows;
        for (size_t n = 0; n < collect->count; ++n) {
            bool found = false;
            for (size_t s = 0; s < process->count; ++s) {
                found |= rows[n].sig_id == signals[s].sig_id;
            }
            if (!found) { return -ENOENT; }
        }
        for (size_t j = 0; j < i; ++j) {
            if (devices[j].collect_table && devices[j].collect_port == d->collect_port &&
                devices[j].collect_addr == d->collect_addr) { return -EEXIST; }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const struct data_device_cfg *d = &devices[i];
        const struct dp_registered_table *table;
        dp_table_registry_find(d->process_table, PRO_DATA_PROCESS, &table);
        output[i] = (DEV_TABLE_S){.dev_id = d->dev_id,
            .ptl_tbl_cfg = {{.proto_type = PRO_DATA_PROCESS,
                             .pst_table = table->rows, .sig_num = table->count}}};
        if (!d->collect_table) { continue; }
        dp_table_registry_find(d->collect_table, PRO_MB_MASTER, &table);
        output[i].port = d->collect_port; output[i].coll_addr = d->collect_addr;
        output[i].ptl_tbl_cfg[1] = (DEV_TBL_CFG_S){.proto_type = PRO_MB_MASTER,
            .pst_table = table->rows, .sig_num = table->count};
    }
    return 0;
}
