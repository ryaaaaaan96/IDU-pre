target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# 内部头文件通过相对路径引用，不添加到公共搜索路径。
target_sources(app PRIVATE
  tables/process.c
)
# 产品本轮仅运行 process；通信实现保留供后续重新接入。
if(CONFIG_IDU_PRE)
  target_sources(app PRIVATE process_runtime.c)
else()
  # 既有通信测试仍验证保留的完整装配。
  target_sources(app PRIVATE protocol.c device_binding.c tables/collect.c tables/transmit.c)
  target_sources_ifdef(CONFIG_IDU_IAPV3 app PRIVATE tables/segments.c)
endif()
