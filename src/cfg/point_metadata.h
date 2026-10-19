#ifndef IDU_POINT_METADATA_H
#define IDU_POINT_METADATA_H

#include <dataprocess/points.h>
#include "generated/symbols.h"

struct idu_collect_model {
    uint16_t sig, model, offset, legacy_cycle;
    uint8_t width;
    struct dp_value initial;
};

struct idu_process_binding {
    uint16_t sig;
    const char *name;
    const char *callback; /* Original callback name, not a function pointer. */
};

const struct idu_collect_model *idu_collect_models(size_t *count);
const struct idu_process_binding *idu_process_bindings(size_t *count);

#endif
