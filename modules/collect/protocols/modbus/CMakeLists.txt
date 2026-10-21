zephyr_library()
# Keep the public port entry available with an explicit -ENOTSUP when disabled.
zephyr_library_sources(port.c)
zephyr_library_sources_ifdef(CONFIG_CO_MODBUS_ADAPTER client.c)
zephyr_library_sources_ifdef(CONFIG_CO_SIMULATOR simulator.c)
zephyr_library_sources_ifdef(CONFIG_CO_MODBUS_RAW_PORT raw.c)
if(CONFIG_CO_MODBUS_ADAPTER)
  # Shared pinned-Zephyr fixes do not enable the transmit service.
  include(../../../transmit/protocols/modbus/compat/modbus.cmake)
endif()
