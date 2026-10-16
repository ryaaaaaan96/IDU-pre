set(idu_modbus_generated ${CMAKE_BINARY_DIR}/idu_modbus)
execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/prepare_modbus.py
          ${ZEPHYR_BASE} ${idu_modbus_generated}
  COMMAND_ERROR_IS_FATAL ANY)
get_target_property(idu_modbus_sources subsys__modbus SOURCES)
foreach(name modbus_raw.c modbus_serial.c modbus_server.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${ZEPHYR_BASE}/subsys/modbus/${name})
  foreach(source IN LISTS idu_modbus_sources)
    if(source MATCHES "(^|/)${name}$")
      list(REMOVE_ITEM idu_modbus_sources ${source})
      list(APPEND idu_modbus_sources ${idu_modbus_generated}/${name})
    endif()
  endforeach()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_LIST_DIR}/prepare_modbus.py)
set_property(TARGET subsys__modbus PROPERTY SOURCES ${idu_modbus_sources})
target_include_directories(subsys__modbus PRIVATE ${CMAKE_CURRENT_LIST_DIR})
zephyr_library_include_directories(${ZEPHYR_BASE}/subsys/modbus)
