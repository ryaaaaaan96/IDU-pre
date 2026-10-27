# 必需的应用服务被禁用时，在配置阶段报错。
set(required_services
  BOOTLOADER_MCUBOOT
  DATAPROCESS
  GPIO ADC)
foreach(feature IN LISTS required_services)
  if(NOT CONFIG_${feature})
    message(FATAL_ERROR "IDU-pre requires CONFIG_${feature}=y")
  endif()
endforeach()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE main.c)
add_subdirectory(protocol)
add_subdirectory(acquisition)
add_subdirectory(status)
add_subdirectory(process)
