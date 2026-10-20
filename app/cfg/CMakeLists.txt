target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
# device.c includes the private table files so pointer/count bindings are constant.
target_sources(app PRIVATE device.c)
