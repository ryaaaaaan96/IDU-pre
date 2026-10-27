/* 产品通信装配；当前 process 独立入口不编译此文件。 */
#include "device_binding.h"
#include <errno.h>
int protocol_bind_devices(const struct data_device_cfg *devices, size_t count,
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
