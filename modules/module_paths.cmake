# 产品和测试共用的模块路径登记；func/platform 只负责源码分类。
include_guard(GLOBAL)
get_filename_component(IDU_MODULES_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

# func：与产品无直接绑定的功能组件。
set(IDU_MODULE_COMMON ${IDU_MODULES_ROOT}/func/common)
set(IDU_MODULE_DATAPROCESS ${IDU_MODULES_ROOT}/func/dataprocess)
set(IDU_MODULE_MODBUS ${IDU_MODULES_ROOT}/func/modbus)
set(IDU_MODULE_TRANSMIT ${IDU_MODULES_ROOT}/func/transmit)
set(IDU_MODULE_IAP ${IDU_MODULES_ROOT}/func/iap)
set(IDU_MODULE_WATCHDOG ${IDU_MODULES_ROOT}/func/watchdog)

# func：端口抽象。
set(IDU_MODULE_VPORT ${IDU_MODULES_ROOT}/func/vport)

# dev：设备和总线物理接入。
set(IDU_MODULE_RS485 ${IDU_MODULES_ROOT}/dev/rs485)
set(IDU_MODULE_FAN ${IDU_MODULES_ROOT}/dev/fan)
set(IDU_MODULE_TPC5120 ${IDU_MODULES_ROOT}/dev/tpc5120)

# platform：SoC、DTS、驱动和上游硬件支持。
set(IDU_MODULE_GD32E505 ${IDU_MODULES_ROOT}/platform/gd32e505)
