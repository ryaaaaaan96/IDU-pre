cmake_minimum_required(VERSION 3.20.0)
set(KCONFIG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Kconfig)
get_filename_component(APP_ROOT ../.. ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${APP_ROOT}/modules/watchdog)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(status_task_watchdog_tests)
target_include_directories(app PRIVATE ${APP_ROOT}/app
  ${APP_ROOT}/modules/dataprocess/include)
target_sources(app PRIVATE src/main.c)
add_subdirectory(${APP_ROOT}/app/status status)
zephyr_ld_options(-Wl,--wrap=sys_reboot)

# 本套件模拟 DP 停止入口，不链接 DP；公开测点类型仍需字容量。
target_compile_definitions(app PRIVATE CONFIG_DP_POINT_WORDS=16)
