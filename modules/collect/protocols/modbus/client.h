#ifndef CO_MODBUS_CLIENT_INTERNAL_H
#define CO_MODBUS_CLIENT_INTERNAL_H

#include <errno.h>
#include <zephyr/modbus/modbus.h>

#if defined(CONFIG_CO_MODBUS_ADAPTER)
int co_modbus_transfer(int iface, struct modbus_adu *adu);
int co_modbus_reconfigure(int iface, struct modbus_iface_param param);
#else
static inline int co_modbus_transfer(int iface, struct modbus_adu *adu)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(adu);
    return -ENOTSUP;
}

static inline int co_modbus_reconfigure(int iface, struct modbus_iface_param param)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(param);
    return -ENOTSUP;
}
#endif

#endif
