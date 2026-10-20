#include <dataprocess/points.h>

#include "signal_ids.h"
#include "device_config.h"
#include "instances.h"

#define MVIO_DEVICE(dev, bus, unit, north, address_reg, period, identity, stale, identity_stale) \
    {.dev_id = dev, .bus_id = bus, .unit_id = unit},

const struct dp_equip_cfg *idu_equipment_config(void)
{
    static const struct dp_device_cfg devices[] = {
        IDU_MVIO_INSTANCES(MVIO_DEVICE)
#if defined(CONFIG_IDU_AIDC_PROFILE)
/* BEGIN IMPORTED TABLE: devices */
/* Generated from user-provided AIDC source tables. Original tables: SUNGROW.
 * Regenerate with tools/import_aidc.py; no closed library is linked. */
    {.dev_id=11,.unit_id=1,.bus_id=1},
    {.dev_id=12,.unit_id=2,.bus_id=1},
    {.dev_id=13,.unit_id=3,.bus_id=1},
    {.dev_id=14,.unit_id=4,.bus_id=1},
    {.dev_id=15,.unit_id=5,.bus_id=1},
    {.dev_id=16,.unit_id=6,.bus_id=1},
    {.dev_id=17,.unit_id=7,.bus_id=1},
    {.dev_id=18,.unit_id=8,.bus_id=1},
/* END IMPORTED TABLE: devices */
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
