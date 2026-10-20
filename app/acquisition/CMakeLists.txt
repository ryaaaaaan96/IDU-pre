target_sources(app PRIVATE acquisition.c inputs.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE boot.c)
