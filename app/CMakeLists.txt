# Fail during configuration if a required application service was disabled.
# Modules remain independently optional for their other consumers and tests.
set(idu_required
  DATAPROCESS DP_PROCESS DP_REFERENCE_TABLES DP_PARAMS COLLECT TRANSMIT
  TR_MODBUS_ADAPTER TR_MODBUS_TRANSPORT TR_SERVER_SERVICE TR_REGISTER_SERVICE
  GPIO ADC ANALOG_INPUT DIGITAL_IO IDU_INPUTS LED WATCHDOG_MONITOR)
if(CONFIG_IDU_COLLECT_VIRTUAL)
  list(APPEND idu_required CO_SIMULATOR)
endif()
if(CONFIG_IDU_AIDC_PROFILE)
  list(APPEND idu_required CO_MODEL_SIMULATOR)
  if(CONFIG_DP_MAX_BUSES LESS 2)
    message(FATAL_ERROR "IDU AIDC requires CONFIG_DP_MAX_BUSES >= 2")
  endif()
else()
  list(APPEND idu_required TR_MVIO)
endif()
foreach(feature IN LISTS idu_required)
  if(NOT CONFIG_${feature})
    message(FATAL_ERROR "IDU-pre requires CONFIG_${feature}=y")
  endif()
endforeach()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE main.c)
add_subdirectory(cfg)
add_subdirectory(acquisition)
add_subdirectory(status)
add_subdirectory(process)
