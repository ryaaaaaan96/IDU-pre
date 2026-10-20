# Raw snapshot declarations are also used by point-table-only applications.
zephyr_include_directories(include)
if(CONFIG_DIGITAL_IO)
  zephyr_library()
  zephyr_library_sources(src/digital_io.c)
  zephyr_library_sources_ifdef(CONFIG_IDU_INPUTS src/idu_inputs.c)
endif()
