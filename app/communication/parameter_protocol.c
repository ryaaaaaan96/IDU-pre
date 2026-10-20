#include "idu_equip.h"
#include "protocol_config.h"
#include <transmit/transmit.h>
#include <errno.h>

static int input_read(uint16_t address, uint16_t *value)
{
    return tr_read_register(4, address, value);
}

static int holding_read(uint16_t address, uint16_t *value)
{
    size_t count;
    const struct idu_parameter_status_register *table = idu_parameter_status_table(&count);
    for (size_t i = 0; i < count; ++i) {
        if (address != table[i].address) { continue; }
        struct dp_param_status status;
        dp_params_get_status(&status);
        switch (table[i].field) {
        case IDU_PARAMETER_FLAGS: *value = status.flags; break;
        case IDU_PARAMETER_ERROR: *value = (uint16_t)(int16_t)status.error; break;
        case IDU_PARAMETER_SEQUENCE_LOW: *value = status.sequence & 0xffff; break;
        case IDU_PARAMETER_SEQUENCE_HIGH: *value = status.sequence >> 16; break;
        default: return -EINVAL;
        }
        return 0;
    }
    return tr_read_register(3, address, value);
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
    size_t command_count;
    const struct idu_parameter_command *commands = idu_parameter_command_table(&command_count);
    for (size_t i = 0; i < command_count; ++i) {
        if (address != commands[i].address) { continue; }
        if (!words || count != 1 || words[0] != commands[i].expected_value) {
            return -EINVAL;
        }
        return commands[i].execute(commands[i].dev_id);
    }
    int iface = tr_modbus_request_iface();
    return tr_write(iface < 0 ? 0 : iface, address, words, count);
}
