cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/io_input
  ${IDU_APP}/modules/health_monitor ${IDU_APP}/modules/led_indicator)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(io_module_tests)
target_sources(app PRIVATE src/main.c src/led.c)
