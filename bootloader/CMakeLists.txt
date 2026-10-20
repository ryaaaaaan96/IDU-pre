cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
list(APPEND BOARD_ROOT ${IDU_APP})
list(APPEND DTS_ROOT ${IDU_APP})
# Discover the SoC in Sysbuild and propagate its module to both child images.
include(${IDU_APP}/modules/hardware.cmake)
set(ZEPHYR_EXTRA_MODULES "${ZEPHYR_EXTRA_MODULES}" CACHE STRING "IDU hardware modules" FORCE)
if(NOT DEFINED mcuboot_EXTRA_DTC_OVERLAY_FILE OR "${mcuboot_EXTRA_DTC_OVERLAY_FILE}" STREQUAL "${IDU_APP}/boards/mcuboot-boot.overlay")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE ${IDU_APP}/bootloader/boot.overlay CACHE STRING "" FORCE)
endif()
# Migrate the former default cached by existing build directories.
if(NOT DEFINED IDU-pre_EXTRA_DTC_OVERLAY_FILE OR "${IDU-pre_EXTRA_DTC_OVERLAY_FILE}" STREQUAL "${IDU_APP}/boards/mcuboot.overlay")
  set(IDU-pre_EXTRA_DTC_OVERLAY_FILE ${IDU_APP}/bootloader/app.overlay CACHE STRING "" FORCE)
endif()
# Migrate the former default cached by existing build directories.
if(NOT DEFINED IDU-pre_EXTRA_CONF_FILE OR "${IDU-pre_EXTRA_CONF_FILE}" STREQUAL "${IDU_APP}/conf/mcuboot.conf")
  set(IDU-pre_EXTRA_CONF_FILE ${IDU_APP}/bootloader/app.conf CACHE STRING "" FORCE)
endif()
# Migrate the former default cached by existing build directories.
if(NOT DEFINED mcuboot_EXTRA_CONF_FILE OR "${mcuboot_EXTRA_CONF_FILE}" STREQUAL "${IDU_APP}/sysbuild/mcuboot.conf")
  set(mcuboot_EXTRA_CONF_FILE ${IDU_APP}/bootloader/mcuboot.conf CACHE STRING "" FORCE)
endif()
find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)
