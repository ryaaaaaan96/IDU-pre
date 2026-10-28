cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ../.. ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/quicklz ${IDU_APP}/modules/iap
  ${IDU_APP}/modules/dataprocess ${IDU_APP}/modules/modbus)
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/common)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iap_receiver_tests)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set(IAP_FIXTURE_ADDRESS 0x08010000)
set(IAP_FIXTURE_RAM_END 0x20020000)
set(IAP_FIXTURE_CONFIG ${IDU_APP}/conf/iapv3.conf)
if(CONFIG_IAP_CONFIGURED)
  # 有意采用不同地址、密钥和限制，验证配置能力而非 IDU 固定常量。
  set(IAP_FIXTURE_ADDRESS 0x10000)
  set(IAP_FIXTURE_RAM_END 0x30020000)
  set(IAP_FIXTURE_CONFIG ${PROJECT_BINARY_DIR}/zephyr/.config)
endif()
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/fixtures.py
  ${CMAKE_CURRENT_BINARY_DIR} ${IAP_FIXTURE_ADDRESS} ${IAP_FIXTURE_RAM_END}
  ${IAP_FIXTURE_CONFIG} COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/fixtures.py ${IDU_APP}/tools/pack_sgz.py
  ${IDU_APP}/modules/iap/scripts/public_key.py ${IDU_APP}/conf/iapv3.conf)
target_sources(app PRIVATE src/main.c)
zephyr_ld_options(-Wl,--wrap=boot_is_img_confirmed -Wl,--wrap=boot_request_upgrade
                  -Wl,--wrap=sys_reboot)
