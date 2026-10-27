#include <dataprocess/points.h>
#include <dataprocess/model.h>
#include <errno.h>
#include <string.h>

struct dp_key dp_config_key(const struct dp_equip_cfg *cfg, size_t index)
{
    if (!cfg || index >= cfg->point_count) { return (struct dp_key){0}; }
    if (cfg->models) {
        for (size_t i = 0; i < cfg->model_count; ++i) {
            const struct dp_model_cfg *model = &cfg->models[i];
            if (index < model->point_count) {
                return (struct dp_key){model->device_id, model->points[index].sig_id};
            }
            index -= model->point_count;
        }
        return (struct dp_key){0};
    }
    if (!cfg->point_get) {
        return cfg->points ? cfg->points[index].key : (struct dp_key){0};
    }
    if (!cfg->reference_devices) {
        struct dp_point_cfg scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, index, &scratch);
        return p ? p->key : (struct dp_key){0};
    }
    for (size_t d = 0; d < cfg->reference_device_count; ++d) {
        const DEV_TABLE_S *dev = &cfg->reference_devices[d];
        const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[0];
        if (index < group->sig_num) {
            const DM_SIG_CFG_S *rows = group->pst_table;
            return (struct dp_key){dev->dev_id, rows[index].sig_id};
        }
        index -= group->sig_num;
    }
    return (struct dp_key){0};
}

const struct dp_point_cfg *dp_config_point(const struct dp_equip_cfg *cfg,
                                          size_t index, struct dp_point_cfg *scratch)
{
    if (!cfg || index >= cfg->point_count) { return NULL; }
    if (!cfg->point_get) { return cfg->points ? &cfg->points[index] : NULL; }
    if (!scratch) { return NULL; }
    memset(scratch, 0, sizeof(*scratch));
    return cfg->point_get(cfg, index, scratch) ? NULL : scratch;
}

const struct co_cfg *dp_config_collect(const struct dp_equip_cfg *cfg,
                                       size_t index, struct co_cfg *scratch)
{
    if (!cfg || index >= cfg->collect_count) { return NULL; }
    if (!cfg->collect_get) { return cfg->collect ? &cfg->collect[index] : NULL; }
    if (!scratch) { return NULL; }
    memset(scratch, 0, sizeof(*scratch));
    return cfg->collect_get(cfg, index, scratch) ? NULL : scratch;
}

const struct dp_transmit_cfg *dp_config_transmit(const struct dp_equip_cfg *cfg,
                                                size_t index, struct dp_transmit_cfg *scratch)
{
    if (!cfg || index >= cfg->transmit_count) { return NULL; }
    if (!cfg->transmit_get) { return cfg->transmit ? &cfg->transmit[index] : NULL; }
    if (!scratch) { return NULL; }
    memset(scratch, 0, sizeof(*scratch));
    return cfg->transmit_get(cfg, index, scratch) ? NULL : scratch;
}

int dp_config_index(const struct dp_equip_cfg *cfg, struct dp_key key)
{
    if (!cfg) { return -EINVAL; }
    if (cfg->models) {
        size_t offset = 0;
        for (size_t i = 0; i < cfg->model_count; ++i) {
            const struct dp_model_cfg *model = &cfg->models[i];
            if (model->device_id == key.dev_id) {
                for (size_t n = 0; n < model->point_count; ++n) {
                    if (model->points[n].sig_id == key.sig_id) { return offset + n; }
                }
                return -ENOENT;
            }
            offset += model->point_count;
        }
        return -ENOENT;
    }
    /* 参考目录支持按键查找，无需计算每个默认值。 */
    if (cfg->point_get && cfg->reference_devices) {
        size_t offset = 0;
        for (size_t d = 0; d < cfg->reference_device_count; ++d) {
            const DEV_TABLE_S *dev = &cfg->reference_devices[d];
            const DEV_TBL_CFG_S *group = &dev->ptl_tbl_cfg[0];
            const DM_SIG_CFG_S *rows = group->pst_table;
            if (group->sig_num > cfg->point_count - offset ||
                (group->sig_num && !rows)) { return -EINVAL; }
            if (dev->dev_id == key.dev_id) {
                for (size_t s = 0; s < group->sig_num; ++s) {
                    if (rows[s].sig_id == key.sig_id) {
                        return offset + s;
                    }
                }
                return -ENOENT;
            }
            offset += group->sig_num;
        }
        return -ENOENT;
    }
    for (size_t i = 0; i < cfg->point_count; ++i) {
        struct dp_point_cfg scratch;
        const struct dp_point_cfg *p = dp_config_point(cfg, i, &scratch);
        if (p && p->key.dev_id == key.dev_id && p->key.sig_id == key.sig_id) {
            return i;
        }
    }
    return -ENOENT;
}

const struct dp_point_cfg *dp_config_find(const struct dp_equip_cfg *cfg,
                                         struct dp_key key, struct dp_point_cfg *scratch)
{
    int index = dp_config_index(cfg, key);
    return index < 0 ? NULL : dp_config_point(cfg, index, scratch);
}
