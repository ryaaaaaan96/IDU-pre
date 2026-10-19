cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND SOC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(${CMAKE_CURRENT_SOURCE_DIR}/modules/modules.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_pre)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE src/main.c src/io.c src/analog.c src/protocol.c
                           src/registers.c src/health.c src/equip.c src/ports/acquisition.c)
target_sources_ifdef(CONFIG_IDU_COLLECT_VIRTUAL app PRIVATE src/ports/virtual.c)
add_subdirectory(cfg/equip)

target_sources(app PRIVATE src/parameter_protocol.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE src/boot.c)
if(CONFIG_IDU_IAPV3)
  add_subdirectory(src/iap)
endif()
