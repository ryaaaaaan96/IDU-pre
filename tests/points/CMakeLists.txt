cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
include(${IDU_APP}/modules/modules.cmake)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_points_tests)
target_include_directories(app PRIVATE ${IDU_APP}/app ${IDU_APP}/tests)
target_sources(app PRIVATE src/main.c)
add_subdirectory(${IDU_APP}/app/protocol app_cfg)
add_subdirectory(${IDU_APP}/app/process point_process)

target_sources(app PRIVATE src/parameters.c)
target_sources(app PRIVATE src/storage_fault.c)

target_sources(app PRIVATE src/modules.c)

# 注入一次后端初始化失败，验证串行回滚。
zephyr_ld_options(-Wl,--wrap=modbus_init_server)
# 仅在外部 NVS 接口的指定步骤注入参数故障。
zephyr_ld_options(-Wl,--wrap=nvs_mount -Wl,--wrap=nvs_write -Wl,--wrap=nvs_delete)

add_subdirectory(${IDU_APP}/tests/fixtures fixtures)
