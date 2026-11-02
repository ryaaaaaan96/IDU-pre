cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
list(APPEND DTS_ROOT ${IDU_APP})
include(${IDU_APP}/modules/modules.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(protocol_port_tests)
target_include_directories(app PRIVATE ${IDU_APP}/app ${IDU_APP}/app/protocol/inc)
target_sources(app PRIVATE src/main.c
  ${IDU_APP}/app/protocol/port.c
  ${IDU_APP}/app/protocol/protocol.c
  ${IDU_APP}/app/protocol/tables/process.c
  ${IDU_APP}/app/protocol/tables/forward.c)
