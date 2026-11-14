target_sources(app PRIVATE acquisition.c di.c analog.c)

if(CONFIG_FAN)
  target_sources(app PRIVATE fan_feedback.c)
endif()
