cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/idu_points)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_mvio_tests)
target_include_directories(app PRIVATE ${IDU_APP}/include)
target_sources(app PRIVATE src/main.c ${IDU_APP}/src/registers.c
  ${IDU_APP}/src/ports/acquisition.c ${IDU_APP}/src/ports/virtual.c
  ${IDU_APP}/src/parameter_protocol.c)
add_subdirectory(${IDU_APP}/cfg/equip equip_cfg)
zephyr_ld_options(-Wl,--wrap=modbus_init_server)
