cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
list(APPEND DTS_ROOT ${IDU_APP})
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/analog_input
  ${IDU_APP}/modules/gpio)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_adc_tests)
target_include_directories(app BEFORE PRIVATE fakes ../../app)
target_sources(app PRIVATE src/driver.c src/application.c)
