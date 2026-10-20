zephyr_include_directories(include)
if(CONFIG_DATAPROCESS)
  zephyr_library()
  zephyr_library_sources(
    src/configuration.c
    src/points.c
    src/data_process.c
    src/reference_config.c
    src/reference_services.c
  )
  zephyr_library_sources_ifdef(CONFIG_DP_PROCESS src/process.c)
  zephyr_library_sources_ifdef(CONFIG_DP_PARAMS src/params.c)
endif()
