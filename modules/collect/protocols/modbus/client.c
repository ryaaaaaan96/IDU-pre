#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <collect/modbus.h>
#endif
#include "client.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

/* Standard functions also work on Zephyr RAW loopback client interfaces. */
static int transfer(int iface, struct modbus_adu *adu)
{
    if (adu->length < 4 || adu->length > sizeof(adu->data)) { return -EINVAL; }
    uint16_t address = sys_get_be16(adu->data), count = sys_get_be16(adu->data + 2);
    uint16_t words[123];
    int err;
    switch (adu->fc) {
    case 3: case 4:
        if (adu->length != 4 || !count || count > 123 || 1 + count * 2 > sizeof(adu->data) || (uint32_t)address + count > 65536) { return -EINVAL; }
        err = adu->fc == 3 ? modbus_read_holding_regs(iface, adu->unit_id, address, words, count) :
                            modbus_read_input_regs(iface, adu->unit_id, address, words, count);
        if (!err) {
            adu->length = 1 + count * 2; adu->data[0] = count * 2;
            for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], adu->data + 1 + 2 * i); }
        }
        return err;
    case 6:
        if (adu->length != 4) { return -EINVAL; }
        return modbus_write_holding_reg(iface, adu->unit_id, address, count);
    case 16:
        if (!count || count > 123 || adu->length != 5 + count * 2 ||
            adu->data[4] != count * 2 || (uint32_t)address + count > 65536) { return -EINVAL; }
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu->data + 5 + i * 2); }
        err = modbus_write_holding_regs(iface, adu->unit_id, address, words, count);
        if (!err) { adu->length = 4; }
        return err;
    default:
#if defined(CONFIG_MODBUS_RAW_ADU)
        return modbus_raw_backend_txn(iface, adu);
#else
        return -ENOTSUP;
#endif
    }
}

int co_modbus_transfer(int iface, uint16_t dev_id, struct modbus_adu *adu)
{
    if (!adu) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
#ifdef CONFIG_CO_MODBUS_RAW_PORT
    return co_modbus_raw_transfer(iface, dev_id, adu, transfer);
#else
    ARG_UNUSED(dev_id);
    return transfer(iface, adu);
#endif
}

int co_modbus_reconfigure(int iface, struct modbus_iface_param param)
{
#ifdef CONFIG_CO_MODBUS_RAW_PORT
    int bound = co_modbus_port_param(iface, &param);
    if (bound) { return bound; }
#endif
    int err = modbus_disable(iface);
    return err ? err : modbus_init_client(iface, param);
}
