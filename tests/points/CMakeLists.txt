cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_points_tests)
target_include_directories(app PRIVATE ${IDU_APP}/include ${IDU_APP}/tests)
target_sources(app PRIVATE src/main.c ${IDU_APP}/src/equipment/registers.c
    ${IDU_APP}/tests/legacy_registers.c
    ${IDU_APP}/src/equipment/equip.c ${IDU_APP}/src/communication/ports/acquisition.c
    ${IDU_APP}/src/communication/ports/virtual.c)
add_subdirectory(${IDU_APP}/cfg/equip equip_cfg)
add_subdirectory(${IDU_APP}/src/process point_process)

target_sources(app PRIVATE ${IDU_APP}/src/communication/parameter_protocol.c)
target_sources(app PRIVATE ${IDU_APP}/src/communication/mvio.c)
target_sources(app PRIVATE src/parameters.c)

target_sources(app PRIVATE src/modules.c)

# Inject one backend initialization failure to verify serial rollback.
zephyr_ld_options(-Wl,--wrap=modbus_init_server)
