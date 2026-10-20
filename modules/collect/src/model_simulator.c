#include <collect/model_simulator.h>
#include <errno.h>
#include <string.h>

int co_model_simulator_read(void *user, uint16_t dev, uint16_t model,
                            uint16_t offset, uint16_t *words, size_t count)
{
    const struct co_model_simulator *config = user;
    if (!config || !words || !count || !config->registry || !config->defaults) { return -EINVAL; }
    if (dev < config->first_device || dev > config->last_device) { return -ENOENT; }
    const UPP_M_MOD_REG *registry = config->registry;
    for (size_t i = 0; i < registry->model_num; ++i) {
        const UPP_M_MOD *m = &registry->p_model_ary[i];
        if (m->mod_id != model) { continue; }
        for (size_t j = 0; j < m->data_num; ++j) {
            const UPP_M_MOD_DATA *row = &m->p_data[j];
            if (row->mod_data_offset != offset || !row->data_len ||
                (row->data_len + 1U) / 2 != count) { continue; }
            DM_SIG_DATA_S defaults = {0};
            bool recover;
            int err = config->defaults(dev, row->sig_id, &defaults, &recover);
            if (err) { return err; }
            if (row->data_len > sizeof(uint32_t) && !defaults.def_value.pst) { return -EINVAL; }
            memset(words, 0, count * sizeof(*words));
            if (row->data_len <= sizeof(uint32_t)) {
                for (size_t k = 0; k < count; ++k) { words[k] = defaults.def_value.u32 >> (16 * k); }
            } else {
                memcpy(words, defaults.def_value.pst, row->data_len);
            }
            return 0;
        }
    }
    return -ENOENT;
}
