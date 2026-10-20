# Shared by the application and Sysbuild before package discovery.
include_guard(GLOBAL)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/gd32e505)
list(REMOVE_DUPLICATES ZEPHYR_EXTRA_MODULES)
