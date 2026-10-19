#include <dataprocess/params.h>

#include "app_sig_def.h"
#include "dev_cfg.h"
#include "idu_process.h"
#include "idu_pre.h"
#include "idu_mvio.h"
#include "../models/mvio.h"
#include "../instances.h"
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "idu_aidc.h"
#endif

#define CONST_U16(sig, val) \
    {.key = {IDU_DEV_LOCAL, sig}, .source = DP_SOURCE_CONSTANT, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .initial.words = {val}}
#define LOCAL_CURRENT(n) \
    {.key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1 + n}, .source = DP_SOURCE_LOCAL, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = 1500, \
     .process = idu_process_current}

#define PARAM_FLAGS (DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE)

#define MVIO_POINTS(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    IDU_MVIO_POINTS(dev, stale, identity_stale),

static const struct dp_point_cfg table[] = {
#include "process_local.inc"

    IDU_MVIO_INSTANCES(MVIO_POINTS)
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "../aidc/generated/points.inc"
#endif
};

const struct dp_point_cfg *idu_process_table(size_t *count)
{
    *count = ARRAY_SIZE(table);
    return table;
}
