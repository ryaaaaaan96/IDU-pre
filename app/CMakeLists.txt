# Fail during configuration if a required application service was disabled.
# Modules remain independently optional for their other consumers and tests.
set(idu_required
  BOOTLOADER_MCUBOOT
  DATAPROCESS DP_PROCESS DP_REFERENCE_TABLES DP_PARAMS COLLECT CO_MODBUS_ADAPTER CO_MODBUS_RAW_PORT TRANSMIT
  TR_MODBUS_ADAPTER TR_MODBUS_RAW_PORT TR_SERVER_SERVICE TR_REGISTER_SERVICE
  GPIO ADC ANALOG_INPUT DIGITAL_IO IDU_INPUTS LED WATCHDOG_MONITOR)
foreach(feature IN LISTS idu_required)
  if(NOT CONFIG_${feature})
    message(FATAL_ERROR "IDU-pre requires CONFIG_${feature}=y")
  endif()
endforeach()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE main.c cfg/ports.c)
add_subdirectory(cfg)
add_subdirectory(acquisition)
add_subdirectory(status)
add_subdirectory(process)
