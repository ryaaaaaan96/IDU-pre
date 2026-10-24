#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <collect/modbus.h>
#endif
#include "client.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <modbus_support/transport.h>

/* Standard functions also work on Zephyr RAW loopback client interfaces. */
static int transfer(int iface, struct modbus_adu *adu)
{
    if (adu->length < 4 || adu->length > sizeof(adu->data)) { return -EINVAL; }
    uint16_t address = sys_get_be16(adu->data), count = sys_get_be16(adu->data + 2);
    if (adu->fc == 6) {
        return adu->length != 4 ? -EINVAL : modbus_write_holding_reg(iface, adu->unit_id, address, count);
    }
    if (adu->fc != 3 && adu->fc != 4 && adu->fc != 16) {
#if defined(CONFIG_MODBUS_RAW_ADU)
        return modbus_raw_backend_txn(iface, adu);
#else
        return -ENOTSUP;
#endif
    }
    if (!count || count > 123 || (uint32_t)address + count > 65536) { return -EINVAL; }
    if (adu->fc == 16) {
        if (adu->length != 5 + count * 2 || adu->data[4] != count * 2) { return -EINVAL; }
    } else if (adu->length != 4 || 1 + count * 2 > sizeof(adu->data)) { return -EINVAL; }
    uint16_t *words = k_malloc(count * sizeof(*words));
    if (!words) { return -ENOMEM; }
    int err;
    if (adu->fc == 16) {
        for (size_t i = 0; i < count; ++i) { words[i] = sys_get_be16(adu->data + 5 + i * 2); }
        err = modbus_write_holding_regs(iface, adu->unit_id, address, words, count);
        if (!err) { adu->length = 4; }
    } else {
        err = adu->fc == 3 ? modbus_read_holding_regs(iface, adu->unit_id, address, words, count) :
                            modbus_read_input_regs(iface, adu->unit_id, address, words, count);
        if (!err) {
            adu->length = 1 + count * 2; adu->data[0] = count * 2;
            for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], adu->data + 1 + 2 * i); }
        }
    }
    k_free(words);
    return err;
}

static int transfer_bounded(int iface, struct modbus_adu *adu, k_timeout_t timeout)
{
    int err = mb_client_begin(iface, timeout);
    if (err) { return err; }
    err = transfer(iface, adu);
    mb_client_end(iface);
    return err;
}

int co_modbus_transfer_timeout(int iface, uint16_t dev_id, struct modbus_adu *adu,
                               k_timeout_t timeout)
{
    if (!adu) { return -EINVAL; }
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
#ifdef CONFIG_CO_MODBUS_RAW_PORT
    return co_modbus_raw_transfer(iface, dev_id, adu, timeout, transfer_bounded);
#else
    ARG_UNUSED(dev_id);
    return transfer_bounded(iface, adu, timeout);
#endif
}

int co_modbus_transfer(int iface, uint16_t dev_id, struct modbus_adu *adu)
{
    return co_modbus_transfer_timeout(iface, dev_id, adu,
                                      K_MSEC(CONFIG_CO_EXCHANGE_TIMEOUT_MS));
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
