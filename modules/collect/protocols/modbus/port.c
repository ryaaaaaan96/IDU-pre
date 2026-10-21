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
        return co_simulator_start(config->simulator, config->simulation);
#else
        return -ENOTSUP;
#endif
    }
    if (!config->iface_name) { return -EINVAL; }
    int iface = modbus_iface_get_by_name(config->iface_name);
    if (iface < 0) { return iface; }
    int err = modbus_init_client(iface, config->param);
    return err ? err : iface;
#else
    return -ENOTSUP;
#endif
}
