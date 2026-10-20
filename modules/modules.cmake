# Register the reusable modules before find_package(Zephyr).
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/hardware.cmake)
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_LIST_DIR}/dataprocess
  ${CMAKE_CURRENT_LIST_DIR}/collect
  ${CMAKE_CURRENT_LIST_DIR}/transmit
  ${CMAKE_CURRENT_LIST_DIR}/quicklz
  ${CMAKE_CURRENT_LIST_DIR}/iap)
