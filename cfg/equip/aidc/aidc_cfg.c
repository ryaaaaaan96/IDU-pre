#include "aidc_cfg.h"
#include <zephyr/sys/util.h>

static const struct idu_aidc_model models[] = {
#include "generated/models.inc"
};

static const struct idu_aidc_handler handlers[] = {
#include "generated/handlers.inc"
};

const struct idu_aidc_model *idu_aidc_models(size_t *count)
{
    *count = ARRAY_SIZE(models);
    return models;
}

const struct idu_aidc_handler *idu_aidc_handlers(size_t *count)
{
    *count = ARRAY_SIZE(handlers);
    return handlers;
}
