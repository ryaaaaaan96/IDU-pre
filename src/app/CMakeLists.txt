target_sources(app PRIVATE startup.c point_service.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE boot.c)
