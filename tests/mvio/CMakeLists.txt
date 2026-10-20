cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_mvio_tests)
target_include_directories(app PRIVATE ${IDU_APP}/app)
target_sources(app PRIVATE src/main.c
  ${IDU_APP}/app/communication/ports/acquisition.c ${IDU_APP}/app/communication/ports/virtual.c
  ${IDU_APP}/app/communication/parameter_protocol.c ${IDU_APP}/app/communication/mvio.c)
add_subdirectory(${IDU_APP}/app/cfg app_cfg)
add_subdirectory(${IDU_APP}/app/communication/config communication_config)
add_subdirectory(${IDU_APP}/app/process point_process)
zephyr_ld_options(-Wl,--wrap=modbus_init_server)
