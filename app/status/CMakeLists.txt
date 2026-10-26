target_sources(app PRIVATE status.c status_led.c task_monitor.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE image_confirm.c)
