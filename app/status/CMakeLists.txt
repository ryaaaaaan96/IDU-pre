target_sources(app PRIVATE status.c led.c watchdog.c task_monitor.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE image_confirm/image_confirm.c)
