#include "idu_equip.h"

const struct idu_equip_cfg *idu_equipment_config(void)
{
    static const struct idu_device_cfg devices[] = {
        {.dev_id = IDU_DEV_REMOTE_IO, .unit_id = 1},
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "aidc/generated/devices.inc"
#endif
    };
    static struct idu_equip_cfg cfg;
    /* Called once by startup before any producer threads are started. */
    if (cfg.points) {
        return &cfg;
    }
    cfg.exact_overlapping_reads = IS_ENABLED(CONFIG_IDU_AIDC_PROFILE);
    cfg.devices = devices;
    cfg.device_count = ARRAY_SIZE(devices);
    cfg.points = idu_process_table(&cfg.point_count);
    cfg.collect = idu_collect_table(&cfg.collect_count);
    cfg.transmit = idu_transmit_table(&cfg.transmit_count);
    return &cfg;
}
