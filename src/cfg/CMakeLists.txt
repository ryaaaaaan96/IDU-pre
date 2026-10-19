target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE
  device_config.c collect.c process.c transmit.c)

if(CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE point_metadata.c)
endif()
