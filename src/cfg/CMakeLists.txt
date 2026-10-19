target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(app PRIVATE
  equipment/device_config.c
  tables/collect.c tables/process.c tables/transmit.c)

if(CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE aidc/aidc_config.c)
endif()
