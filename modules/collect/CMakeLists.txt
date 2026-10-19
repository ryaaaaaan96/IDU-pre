zephyr_library()
zephyr_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_COLLECT src/collect.c)
