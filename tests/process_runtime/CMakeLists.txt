cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(process_runtime_tests)
target_include_directories(app PRIVATE ${IDU_APP}/app ${IDU_APP}/app/protocol/inc)
target_sources(app PRIVATE src/main.c
  ${IDU_APP}/app/protocol/process_runtime.c
  ${IDU_APP}/app/protocol/tables/process.c
  ${IDU_APP}/app/process/points.c)
