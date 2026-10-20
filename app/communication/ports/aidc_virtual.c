#include "communication/ports/idu_virtual.h"
#include "cfg/inc/device.h"
#include "cfg/inc/signal_ids.h"
#include <collect/collect.h>
#include <errno.h>
#include <string.h>

static int model_read(void *user, uint16_t dev, uint16_t model, uint16_t offset,
                       uint16_t *words, size_t count)
{
    ARG_UNUSED(user);
    if (dev < IDU_DEV_DSP_FIRST || dev > IDU_DEV_DSP_LAST) { return -ENOENT; }
    uint16_t field_count;
    const UPP_M_MOD_REG *registry = idu_model_table(&field_count);
    if (!registry) { return -ENOENT; }
    for (size_t i = 0; i < registry->model_num; ++i) {
        const UPP_M_MOD *m = &registry->p_model_ary[i];
        if (m->mod_id != model) { continue; }
        for (size_t j = 0; j < m->data_num; ++j) {
            const UPP_M_MOD_DATA *row = &m->p_data[j];
            if (row->mod_data_offset != offset || (row->data_len + 1U) / 2 != count) {
                continue;
            }
            DM_SIG_DATA_S defaults;
            bool recover;
            int err = idu_point_default(dev, row->sig_id, &defaults, &recover);
            if (err) { return err; }
            memset(words, 0, count * sizeof(*words));
            if (row->data_len <= sizeof(uint32_t)) {
                for (size_t k = 0; k < count; ++k) {
                    words[k] = defaults.def_value.u32 >> (16 * k);
                }
            } else {
                memcpy(words, defaults.def_value.pst, row->data_len);
            }
            return 0;
        }
    }
    return -ENOENT;
}

int idu_aidc_bus_init(const struct dp_equip_cfg *cfg)
{
    struct co_bus_cfg bus = {.id=1,.iface=-1,.simulated=true,.read_model=model_read};
    return co_init_bus(cfg, &bus);
}
