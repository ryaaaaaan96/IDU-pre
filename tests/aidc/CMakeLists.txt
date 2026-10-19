cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
list(APPEND EXTRA_CONF_FILE ${IDU_APP}/conf/aidc-virtual.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_aidc_tests)
target_include_directories(app PRIVATE ${IDU_APP}/include)
target_sources(app PRIVATE src/main.c
    ${IDU_APP}/src/app/startup.c ${IDU_APP}/src/app/point_service.c
    ${IDU_APP}/src/communication/ports/acquisition.c
    ${IDU_APP}/src/communication/ports/virtual.c ${IDU_APP}/src/communication/parameter_protocol.c)
add_subdirectory(${IDU_APP}/src/cfg app_cfg)
add_subdirectory(${IDU_APP}/src/communication/config communication_config)
add_subdirectory(${IDU_APP}/src/process point_process)
target_sources(app PRIVATE ${IDU_APP}/src/communication/aidc_serial.c
  ${IDU_APP}/src/communication/ports/aidc_virtual.c)
