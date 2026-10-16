#include "idu_equip.h"
#include <idu/forward.h>
#include <errno.h>

static int input_read(uint16_t address, uint16_t *value)
{
    return idu_points_read_register(4, address, value);
}

static int holding_read(uint16_t address, uint16_t *value)
{
    if (address >= 32000 && address <= 32003) {
        struct idu_param_status status;
        idu_params_get_status(&status);
        switch (address) {
        case 32000: *value = status.flags; break;
        case 32001: *value = (uint16_t)(int16_t)status.error; break;
        case 32002: *value = status.sequence & 0xffff; break;
        case 32003: *value = status.sequence >> 16; break;
        }
        return 0;
    }
    return idu_points_read_register(3, address, value);
}

struct modbus_user_callbacks *idu_parameter_callbacks(void)
{
    static struct modbus_user_callbacks callbacks = {
        .input_reg_rd = input_read, .holding_reg_rd = holding_read,
    };
    return &callbacks;
}

int idu_parameter_write(uint16_t address, const uint16_t *words, size_t count)
{
    if (address >= 31090 && address <= 31093) {
        if (!words || count != 1 || words[0] != (address <= 31091 ? 0xa55a : 1)) {
            return -EINVAL;
        }
        switch (address) {
        case 31090: return idu_params_restore(IDU_DEV_LOCAL);
        case 31091: return idu_params_restore(IDU_DEV_REMOTE_IO);
        case 31092: return idu_params_flush();
        case 31093: return idu_params_recover();
        }
    }
    int iface = idu_modbus_request_iface();
    return idu_forward_write(iface < 0 ? 0 : iface, address, words, count);
}

uint16_t idu_sample_period_ms(void)
{
    struct idu_point_state state;
    int err = idu_params_read((struct idu_key){IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD}, &state);
    return err ? CONFIG_IDU_SAMPLE_PERIOD_MS : state.value.words[0];
}
