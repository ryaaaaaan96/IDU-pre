#include <dataprocess/acquisition.h>
#include "acquisition_internal.h"
#include <dataprocess/params.h>
#if defined(CONFIG_DP_PROCESS)
#include <dataprocess/process.h>
#endif
#include <errno.h>

int dp_accept_samples(struct dp_point_batch *batch, bool sync)
{
    int err = dp_publish(batch, K_MSEC(20));
    if (err || !sync) { return err; }
    err = dp_wait_processed(batch->ticket, K_SECONDS(1));
    for (size_t i = 0; !err && i < batch->count; ++i) {
        struct dp_point_state state;
        err = dp_read(batch->points[i].key, &state);
        if (!err && (state.quality & 0xff) != DP_QUALITY_GOOD) {
            err = state.error ? state.error : -EIO;
        }
    }
    return err;
}

int dp_initialize_acquired_device(uint16_t dev_id)
{
    int err = 0;
#if defined(CONFIG_DP_PROCESS)
    err = dp_process_init_device(dev_id);
#endif
#if defined(CONFIG_DP_PARAMS)
    if (!err) { err = dp_params_sync_device(dev_id); }
#endif
    return err;
}

bool dp_acquired_key(const struct dp_equip_cfg *cfg, struct dp_key key)
{
    for (size_t i = 0; i < cfg->collect_count; ++i) {
        struct co_cfg scratch;
        const struct co_cfg *poll = dp_config_collect(cfg, i, &scratch);
        if (poll->dev_id != key.dev_id) { continue; }
        for (size_t j = 0; j < poll->field_count; ++j) {
            const struct co_field *f = &poll->fields[j];
            if (f->sig_id == key.sig_id) { return true; }
        }
    }
    return false;
}
