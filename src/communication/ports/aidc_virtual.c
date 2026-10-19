#include "idu_aidc.h"
#include "aidc_config.h"
#include <collect/collect.h>
#include <errno.h>
#include <string.h>

static int model_read(void *user, uint16_t dev, uint16_t model, uint16_t offset,
                       uint16_t *words, size_t count)
{
    ARG_UNUSED(user);
    if (dev < 11 || dev > 18) { return -ENOENT; }
    size_t model_count;
    const struct idu_aidc_model *models = idu_aidc_models(&model_count);
    for (size_t i = 0; i < model_count; ++i) {
        const struct idu_aidc_model *m = &models[i];
        if (m->model == model && m->offset == offset && m->width == count) {
            memcpy(words, m->initial.words, count * sizeof(*words)); return 0;
        }
    }
    return -ENOENT;
}

int idu_aidc_bus_init(const struct dp_equip_cfg *cfg)
{
    struct co_bus_cfg bus = {.id=1,.iface=-1,.simulated=true,.read_model=model_read};
    return co_init_bus(cfg, &bus);
}
