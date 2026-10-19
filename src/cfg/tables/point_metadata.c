#include "point_metadata.h"
#include <zephyr/sys/util.h>

static const struct idu_collect_model models[] = {
#include "generated/models.inc"
};

static const struct idu_process_binding handlers[] = {
#include "generated/handlers.inc"
};

const struct idu_collect_model *idu_collect_models(size_t *count)
{
    *count = ARRAY_SIZE(models);
    return models;
}

const struct idu_process_binding *idu_process_bindings(size_t *count)
{
    *count = ARRAY_SIZE(handlers);
    return handlers;
}
