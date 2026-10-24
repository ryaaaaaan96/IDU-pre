zephyr_library()
# 禁用时仍保留公共端口入口，并明确返回 -ENOTSUP。
zephyr_library_sources(port.c)
zephyr_library_sources_ifdef(CONFIG_CO_MODBUS_ADAPTER client.c)
zephyr_library_sources_ifdef(CONFIG_CO_SIMULATOR simulator.c)
zephyr_library_sources_ifdef(CONFIG_CO_MODBUS_RAW_PORT raw.c)
