target_sources(app PRIVATE status.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE image_confirm/image_confirm.c)
add_subdirectory(task_watchdog)
