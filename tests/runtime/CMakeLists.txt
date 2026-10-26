cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_runtime_tests)
target_include_directories(app PRIVATE ${IDU_APP}/app)
target_sources(app PRIVATE src/main.c ${IDU_APP}/app/main.c
  ${IDU_APP}/app/protocol/init.c
  ${IDU_APP}/app/protocol/tasks.c ${IDU_APP}/app/protocol/ports.c)
set_source_files_properties(${IDU_APP}/app/main.c PROPERTIES COMPILE_DEFINITIONS "main=application_main")
add_subdirectory(${IDU_APP}/app/status status)
add_subdirectory(${IDU_APP}/app/protocol app_cfg)
add_subdirectory(${IDU_APP}/app/process point_process)
zephyr_ld_options(-Wl,--wrap=k_malloc -Wl,--wrap=sys_reboot -Wl,--wrap=flash_area_open)

include(${CMAKE_CURRENT_LIST_DIR}/../fixtures/acquisition.cmake)
