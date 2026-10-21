include_guard(GLOBAL)
set(tr_modbus_generated ${CMAKE_BINARY_DIR}/tr_modbus)
execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/prepare_modbus.py
          ${ZEPHYR_BASE} ${tr_modbus_generated}
  COMMAND_ERROR_IS_FATAL ANY)
get_target_property(tr_modbus_sources subsys__modbus SOURCES)
foreach(name modbus_core.c modbus_raw.c modbus_serial.c modbus_server.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${ZEPHYR_BASE}/subsys/modbus/${name})
  foreach(source IN LISTS tr_modbus_sources)
    if(source MATCHES "(^|/)${name}$")
      list(REMOVE_ITEM tr_modbus_sources ${source})
      list(APPEND tr_modbus_sources ${tr_modbus_generated}/${name})
    endif()
  endforeach()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_LIST_DIR}/prepare_modbus.py)
set_property(TARGET subsys__modbus PROPERTY SOURCES ${tr_modbus_sources})
target_include_directories(subsys__modbus PRIVATE ${CMAKE_CURRENT_LIST_DIR})
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})
zephyr_library_include_directories(${ZEPHYR_BASE}/subsys/modbus)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/serial_open.h ${CMAKE_CURRENT_LIST_DIR}/core_io.h)
