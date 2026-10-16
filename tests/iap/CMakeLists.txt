cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iap_receiver_tests)
get_filename_component(IDU_APP ../.. ABSOLUTE)
set(CONFIG_MCUBOOT_SIGNATURE_KEY_FILE ${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ec-p256.pem)
target_include_directories(app PRIVATE ${IDU_APP}/include ${IDU_APP}/modules/idu_points/include
  ${CMAKE_CURRENT_BINARY_DIR})
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/fixtures.py
  ${CMAKE_CURRENT_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/fixtures.py ${IDU_APP}/tools/pack_sgz.py)
target_sources(app PRIVATE src/main.c)
add_subdirectory(${IDU_APP}/src/iap iap)
zephyr_ld_options(-Wl,--wrap=boot_is_img_confirmed -Wl,--wrap=boot_request_upgrade
                  -Wl,--wrap=sys_reboot)
