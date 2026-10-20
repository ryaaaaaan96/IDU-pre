target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_sources(app PRIVATE
  device.c tables/collect.c tables/process.c tables/transmit.c)
