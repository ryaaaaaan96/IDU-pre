#ifndef IDU_DEV_CFG_H
#define IDU_DEV_CFG_H

#include <stddef.h>

struct dp_equip_cfg;
struct dp_point_cfg;
struct co_cfg;
struct dp_transmit_cfg;

/* Assemble immutable communication tables before starting any producers. */
const struct dp_equip_cfg *idu_equipment_config(void);
const struct dp_point_cfg *idu_process_table(size_t *count);
const struct co_cfg *idu_collect_table(size_t *count);
const struct dp_transmit_cfg *idu_transmit_table(size_t *count);

#endif
