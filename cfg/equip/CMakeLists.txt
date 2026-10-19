target_sources(app PRIVATE dev_cfg.c table/collect.c table/process.c table/transmit.c)

target_sources_ifdef(CONFIG_IDU_AIDC_PROFILE app PRIVATE aidc/aidc.c)
