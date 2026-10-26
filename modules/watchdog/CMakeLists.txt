zephyr_library()
zephyr_include_directories(include)
# 关闭软件监控时保留接口，初始化与登记均拒绝启用监控。
zephyr_library_sources(src/task_monitor.c)
zephyr_library_sources_ifdef(CONFIG_WATCHDOG_EXTERNAL src/gpio_watchdog.c)
