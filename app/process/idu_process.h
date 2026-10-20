#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

struct dp_point_cfg;
struct dp_value;
struct dp_key;
struct dp_process_ctx;

int idu_process_current(const struct dp_point_cfg *cfg, struct dp_value *value,
                        const struct dp_value *old_value);
int idu_process_restore(uint16_t dev);
int idu_process_flush(uint16_t unused);
int idu_process_recover(uint16_t unused);

struct idu_snapshot;

/* Application-thread services; callers do not need point-table or NVS internals. */
int idu_equipment_init(void);
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);
bool idu_equipment_healthy(uint32_t max_idle_ms);
#ifdef CONFIG_BOOTLOADER_MCUBOOT
bool idu_equipment_confirm_ready(void);
#endif

/* Profile-specific point handlers; selected by the product process table. */
int idu_mvio_seed_address(const struct dp_point_cfg *cfg, struct dp_value *value);
int idu_mvio_prepare_address(const struct dp_point_cfg *cfg, struct dp_value *value,
                             const struct dp_value *old);
int idu_aidc_process(struct dp_process_ctx *ctx, struct dp_key key);

#endif
