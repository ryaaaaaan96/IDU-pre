cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/dataprocess)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(table_registry_tests)
target_sources(app PRIVATE src/main.c src/process.c src/collect.c src/transmit.c)

target_sources(app PRIVATE ${IDU_APP}/app/protocol/device_binding.c)
