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
  zephyr_library_sources_ifdef(CONFIG_DP_PARAMS src/params.c src/parameter_store.c)
endif()

if(CONFIG_DP_TABLE_REGISTRY)
  zephyr_library_sources(src/table_registry.c)
  zephyr_linker_sources(SECTIONS tables.ld)
  zephyr_iterable_section(NAME dp_registered_table GROUP RODATA_REGION)
endif()
