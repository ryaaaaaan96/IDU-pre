target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# 内部头文件通过相对路径引用，不添加到公共搜索路径。
target_sources(app PRIVATE
  protocol.c
  tables/process.c
  tables/collect.c
  tables/transmit.c
)
target_sources_ifdef(CONFIG_IDU_IAPV3 app PRIVATE tables/segments.c)

if(CONFIG_COLLECT)
  target_sources(app PRIVATE connections.c)
endif()
