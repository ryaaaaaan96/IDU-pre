cmake_minimum_required(VERSION 3.20.0)
get_filename_component(IDU_APP ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
list(APPEND DTS_ROOT ${IDU_APP})
list(APPEND ZEPHYR_EXTRA_MODULES ${IDU_APP}/modules/analog_input
  ${IDU_APP}/modules/gpio ${IDU_APP}/modules/input_sampler)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(idu_adc_tests)
target_include_directories(app BEFORE PRIVATE fakes ../../app)
target_sources(app PRIVATE src/driver.c src/application.c ../../app/acquisition/config.c)
# 测试原生 ADC 后端时，仍保留产品的十次采样契约。
target_compile_definitions(app PRIVATE CONFIG_IDU_ADC_AVERAGE_COUNT=10)
