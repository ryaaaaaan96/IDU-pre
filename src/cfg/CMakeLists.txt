target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/devices
  ${CMAKE_CURRENT_SOURCE_DIR}/tables)
target_sources(app PRIVATE
  devices/device_config.c tables/collect.c tables/process.c tables/transmit.c)

if(CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE tables/point_metadata.c)
endif()
