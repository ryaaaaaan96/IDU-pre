#ifndef IDU_AIDC_CONFIG_H
#define IDU_AIDC_CONFIG_H

#include <dataprocess/points.h>
#include "../aidc/generated/symbols.h"

struct idu_aidc_model {
    uint16_t sig, model, offset, legacy_cycle;
    uint8_t width;
    struct dp_value initial;
};

struct idu_aidc_handler {
    uint16_t sig;
    const char *name, *callback;
};

const struct idu_aidc_model *idu_aidc_models(size_t *count);
const struct idu_aidc_handler *idu_aidc_handlers(size_t *count);

#endif
