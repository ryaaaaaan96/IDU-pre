#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <collect/modbus.h>
#endif
#include <collect/port.h>
#ifdef CONFIG_CO_SIMULATOR
#include <collect/simulator.h>
#endif
#include <errno.h>

int co_port_open(const struct co_port_config *config)
{
    if (!config) { return -EINVAL; }
#if defined(CONFIG_CO_MODBUS_ADAPTER)
    if (config->simulator) {
#ifdef CONFIG_CO_SIMULATOR
        return co_simulator_init(config->simulator, config->simulation);
#else
        return -ENOTSUP;
#endif
    }
    if (!config->iface_name) { return -EINVAL; }
    int iface = modbus_iface_get_by_name(config->iface_name);
    if (iface < 0) { return iface; }
    struct modbus_iface_param param = config->param;
#ifdef CONFIG_CO_MODBUS_RAW_PORT
    int bound = co_modbus_port_param(iface, &param);
    if (bound) { return bound; }
#endif
    int err = modbus_init_client(iface, param);
    return err ? err : iface;
#else
    return -ENOTSUP;
#endif
}
