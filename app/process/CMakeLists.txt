target_sources(app PRIVATE points.c engineering.c)
if(CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE aidc_process.c)
endif()
