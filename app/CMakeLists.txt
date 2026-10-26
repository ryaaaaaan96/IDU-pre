# 必需的应用服务被禁用时，在配置阶段报错。
# 各模块供其他调用方和测试使用时，仍可独立选择启用。
set(required_services
  BOOTLOADER_MCUBOOT
  DATAPROCESS DP_PROCESS DP_REFERENCE_TABLES DP_TABLE_REGISTRY DP_PARAMS COLLECT CO_MODBUS_ADAPTER CO_MODBUS_RAW_PORT TRANSMIT
  TR_MODBUS_ADAPTER TR_MODBUS_RAW_PORT TR_SERVER_SERVICE TR_REGISTER_SERVICE
  GPIO ADC ANALOG_IO DIGITAL_IO WATCHDOG_MONITOR)
foreach(feature IN LISTS required_services)
  if(NOT CONFIG_${feature})
    message(FATAL_ERROR "IDU-pre requires CONFIG_${feature}=y")
  endif()
endforeach()

# 旧版 IAP 接收器会忽略站号 247；此产品组合应在
# 开放服务前拒绝，但不限制未启用 IAP 时使用站号 247。
if(CONFIG_IDU_IAPV3 AND CONFIG_IDU_FORWARD_UNIT EQUAL 247)
  message(FATAL_ERROR "IDU-pre IAPv3 does not support CONFIG_IDU_FORWARD_UNIT=247")
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE main.c protocol/ports.c)
add_subdirectory(protocol)
add_subdirectory(acquisition)
add_subdirectory(status)
add_subdirectory(process)
