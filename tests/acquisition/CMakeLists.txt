cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_acquisition_tests)
# Use the public IAP contract while mocking its startup in this lifecycle suite.
target_include_directories(app PRIVATE ../../app ../../modules/iap/include)
target_sources(app PRIVATE src/main.c ../../app/acquisition/acquisition.c)
# Exercise optional orchestration with mocked services, without a bootloader/HAL.
target_compile_definitions(app PRIVATE CONFIG_BOOTLOADER_MCUBOOT=1 CONFIG_IDU_IAPV3=1)
