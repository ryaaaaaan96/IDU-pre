zephyr_library()
# Keep the public port entry available with an explicit -ENOTSUP when disabled.
zephyr_library_sources(port.c)
zephyr_library_sources_ifdef(CONFIG_CO_MODBUS_ADAPTER client.c)
zephyr_library_sources_ifdef(CONFIG_CO_SIMULATOR simulator.c)
