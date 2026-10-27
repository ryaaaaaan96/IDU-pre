# 故障接口可从定时器 ISR 调用，日志后端必须在线程中执行。
if(CONFIG_LOG AND NOT CONFIG_LOG_MODE_DEFERRED)
  message(FATAL_ERROR "status requires CONFIG_LOG_MODE_DEFERRED=y when logging is enabled")
endif()
if(CONFIG_LOG_BLOCK_IN_THREAD)
  message(FATAL_ERROR "status requires CONFIG_LOG_BLOCK_IN_THREAD=n")
endif()

target_sources(app PRIVATE status.c status_led.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE image_confirm.c)
