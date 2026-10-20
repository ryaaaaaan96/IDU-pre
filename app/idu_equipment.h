#ifndef IDU_EQUIPMENT_H
#define IDU_EQUIPMENT_H

#include <stdbool.h>
#include <stdint.h>

struct idu_snapshot;

/* Application-thread services; callers do not need point-table or NVS internals. */
int idu_equipment_init(void);
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);
bool idu_equipment_healthy(uint32_t max_idle_ms);
#ifdef CONFIG_BOOTLOADER_MCUBOOT
bool idu_equipment_boot_ready(void);
#endif

#endif
