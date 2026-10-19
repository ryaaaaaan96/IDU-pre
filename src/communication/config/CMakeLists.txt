target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE protocol_config.c parameter_config.c)

if(NOT CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE mvio_config.c)
endif()
