target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# 内部头文件通过相对路径引用，不添加到公共搜索路径。
target_sources(app PRIVATE
  tables/process.c
  process_runtime.c
)
