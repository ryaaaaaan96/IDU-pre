target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# 产品只提供点表和模块配置，执行代码由 process 或通用模块负责。
target_sources(app PRIVATE
  device.c
  ports.c
  forwarding.c
  parameters.c
  tables/process.c
  tables/collect.c
  tables/transmit.c
)

target_sources_ifdef(CONFIG_IDU_COLLECT_VIRTUAL app PRIVATE simulation.c)
