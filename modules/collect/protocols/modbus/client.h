#ifndef CO_MODBUS_CLIENT_INTERNAL_H
#define CO_MODBUS_CLIENT_INTERNAL_H

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>

#if defined(CONFIG_CO_MODBUS_ADAPTER)
int co_modbus_transfer(int iface, uint16_t dev_id, struct modbus_adu *adu);
int co_modbus_transfer_timeout(int iface, uint16_t dev_id, struct modbus_adu *adu,
                               k_timeout_t timeout);
int co_modbus_reconfigure(int iface, struct modbus_iface_param param);
#else
static inline int co_modbus_transfer_timeout(int iface, uint16_t dev_id,
                                              struct modbus_adu *adu, k_timeout_t timeout)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(dev_id);
    ARG_UNUSED(adu);
    ARG_UNUSED(timeout);
    return -ENOTSUP;
}

static inline int co_modbus_reconfigure(int iface, struct modbus_iface_param param)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(param);
    return -ENOTSUP;
}
#endif

#ifdef CONFIG_CO_MODBUS_RAW_PORT
/* Pin device identity for the complete synchronous transaction. */
int co_modbus_raw_transfer(int iface, uint16_t dev_id, struct modbus_adu *adu,
                          k_timeout_t timeout,
                          int (*transfer)(int, struct modbus_adu *, k_timeout_t));
#endif

#endif
