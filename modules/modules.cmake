# 在 find_package(Zephyr) 前登记通用模块。
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/hardware.cmake)
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_LIST_DIR}/analog_input
  ${CMAKE_CURRENT_LIST_DIR}/gpio
  ${CMAKE_CURRENT_LIST_DIR}/input_sampler
  ${CMAKE_CURRENT_LIST_DIR}/watchdog
  ${CMAKE_CURRENT_LIST_DIR}/vport
  ${CMAKE_CURRENT_LIST_DIR}/dataprocess
  ${CMAKE_CURRENT_LIST_DIR}/modbus
  ${CMAKE_CURRENT_LIST_DIR}/collect
  ${CMAKE_CURRENT_LIST_DIR}/transmit
  ${CMAKE_CURRENT_LIST_DIR}/quicklz
  ${CMAKE_CURRENT_LIST_DIR}/iap)
