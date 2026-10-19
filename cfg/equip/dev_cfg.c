#include "idu_equip.h"
#include "instances.h"

#define MVIO_DEVICE(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    {.dev_id = dev, .bus_id = bus, .unit_id = unit},

const struct dp_equip_cfg *idu_equipment_config(void)
{
    static const struct dp_device_cfg devices[] = {
        IDU_MVIO_INSTANCES(MVIO_DEVICE)
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "aidc/generated/devices.inc"
#endif
    };
    static struct dp_equip_cfg cfg;
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
