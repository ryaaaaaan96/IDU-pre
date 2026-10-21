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

int dp_table_bind(const struct dp_device_binding *devices, size_t count,
                  const struct dp_registered_table *tables, size_t table_count,
                  DEV_TABLE_S *output)
{
    if (!devices || !count || !tables || !table_count || !output) { return -EINVAL; }
    for (size_t i = 0; i < table_count; ++i) {
        const struct dp_registered_table *t = &tables[i];
        if (!t->id || !t->rows || !t->count || t->count > UINT16_MAX ||
            (t->type != PRO_DATA_PROCESS && t->type != PRO_MB_MASTER &&
             t->type != PRO_MB_SLAVE && t->type != PRO_UPP_MASTER)) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            if (tables[j].id == t->id) { return -EEXIST; }
        }
    }
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
