# Register the reusable modules before find_package(Zephyr).
include_guard(GLOBAL)
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_LIST_DIR}/dataprocess
  ${CMAKE_CURRENT_LIST_DIR}/collect
  ${CMAKE_CURRENT_LIST_DIR}/transmit
  ${CMAKE_CURRENT_LIST_DIR}/quicklz)
