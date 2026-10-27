cmake_minimum_required(VERSION 3.20.0)
set(KCONFIG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Kconfig)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/app.overlay)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/watchdog)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_acquisition_tests)
# 按公共契约编译产品编排代码，服务由测试模拟。
target_include_directories(app PRIVATE ../../app ../../modules/iap/include
  ../../modules/watchdog/include ../../modules/transmit/include
  ../../modules/dataprocess/include ../../modules/collect/include ../../modules/vport/include)
target_sources(app PRIVATE src/main.c ../../app/main.c ../../app/protocol/init.c)
add_subdirectory(../../app/status status)
# main 由 Ztest 提供，将真实应用的启动函数提供给测试夹具。
set_source_files_properties(../../app/main.c PROPERTIES
  COMPILE_DEFINITIONS "main=application_main")
# 通过服务模拟验证可选编排，不使用引导程序或 HAL。
target_compile_definitions(app PRIVATE CONFIG_BOOTLOADER_MCUBOOT=1 CONFIG_IDU_IAPV3=1
  CONFIG_DP_POINT_WORDS=16 CONFIG_DP_PROCESS_MAX_OUTPUTS=24 CONFIG_DP_MAX_DEVICES=1
  CONFIG_IDU_COLLECT_RETRIES=2 CONFIG_IDU_COLLECT_EXCEPTION_MS=3000
  CONFIG_IDU_COLLECT_PROBE_MS=5000 CONFIG_IDU_FORWARD_UNIT=16)

zephyr_ld_options(-Wl,--wrap=watchdog_init -Wl,--wrap=sys_reboot)
