zephyr_include_directories(include)
if(CONFIG_DATAPROCESS)
  zephyr_library()
  zephyr_library_sources(
    src/points.c
    src/params.c
  )
endif()
