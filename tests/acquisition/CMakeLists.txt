cmake_minimum_required(VERSION 3.20.0)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_acquisition_tests)
# Use the public IAP contract while mocking its startup in this lifecycle suite.
target_include_directories(app PRIVATE ../../app ../../modules/iap/include
  ../../modules/gpio/include ../../modules/watchdog/include)
target_sources(app PRIVATE src/main.c ../../app/acquisition/acquisition.c ../../app/status/status.c)
# Exercise optional orchestration with mocked services, without a bootloader/HAL.
target_compile_definitions(app PRIVATE CONFIG_BOOTLOADER_MCUBOOT=1 CONFIG_IDU_IAPV3=1)
