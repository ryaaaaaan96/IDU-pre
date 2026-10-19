target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../inc)
target_sources(app PRIVATE dev_cfg.c callback_function.c
  table/collect.c table/process.c table/transmit.c table/transmit_parameters.c)

if(CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE aidc/aidc_cfg.c)
else()
  target_sources(app PRIVATE table/transmit_mvio.c)
endif()
