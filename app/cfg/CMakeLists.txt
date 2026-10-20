target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_sources(app PRIVATE
  device.c
  tables/process.c
  tables/collect.c
  tables/transmit.c
)

# 产品端口/协议只声明模块配置；执行代码归 collect / transmit。
target_sources(app PRIVATE ports.c forwarding.c parameters.c)
target_sources_ifdef(CONFIG_IDU_COLLECT_VIRTUAL app PRIVATE simulation.c)
if(NOT CONFIG_IDU_AIDC_PROFILE)
  target_sources(app PRIVATE mvio.c)
endif()
