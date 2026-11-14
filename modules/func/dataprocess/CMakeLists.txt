zephyr_include_directories(include)
if(CONFIG_DATAPROCESS)
  zephyr_library()
  zephyr_library_sources_ifdef(CONFIG_DP_SHELL src/dp_shell.c)
  zephyr_library_sources(
    src/points.c
    src/params.c
  )
endif()
