#include "process/idu_process.h"
#include "hardware/idu_pre.h"
#include "signal_ids.h"
#include <dataprocess/params.h>
#include <errno.h>

int idu_process_current(const struct dp_point_cfg *cfg, struct dp_value *value,
                        const struct dp_value *old_value)
{
    ARG_UNUSED(cfg);
    ARG_UNUSED(old_value);
    if (value->words[0] > 4095) {
        return -ERANGE;
    }
    value->words[0] = idu_current_from_raw(value->words[0]);
    return 0;
}

uint16_t idu_sample_period_ms(void)
{
    struct dp_point_state state;
    int err = dp_params_read((struct dp_key){IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD}, &state);
    return err ? CONFIG_IDU_SAMPLE_PERIOD_MS : state.value.words[0];
}

int idu_process_restore(uint16_t dev)
{
    return dp_params_restore(dev);
}

int idu_process_flush(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_flush();
}

int idu_process_recover(uint16_t unused)
{
    ARG_UNUSED(unused);
    return dp_params_recover();
}
