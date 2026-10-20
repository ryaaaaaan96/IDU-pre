cmake_minimum_required(VERSION 3.20.0)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_acquisition_tests)
# Compile product orchestration against public contracts; services are mocked.
target_include_directories(app PRIVATE ../../app ../../modules/iap/include
  ../../modules/gpio/include ../../modules/watchdog/include ../../modules/transmit/include
  ../../modules/dataprocess/include ../../modules/collect/include ../../modules/vport/include)
target_sources(app PRIVATE src/main.c ../../app/main.c
  ../../app/acquisition/acquisition.c ../../app/status/status.c)
# Ztest owns main; expose the real application's startup function to the fixture.
set_source_files_properties(../../app/main.c PROPERTIES
  COMPILE_DEFINITIONS "main=idu_application_main")
# Exercise optional orchestration with mocked services, without a bootloader/HAL.
target_compile_definitions(app PRIVATE CONFIG_BOOTLOADER_MCUBOOT=1 CONFIG_IDU_IAPV3=1
  CONFIG_IDU_COLLECT_VIRTUAL=1 CONFIG_DP_POINT_WORDS=16 CONFIG_DP_PROCESS_MAX_OUTPUTS=24)
