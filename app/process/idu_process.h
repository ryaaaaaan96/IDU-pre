#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <dataprocess/points.h>

struct dp_process_ctx;

int idu_process_current(const struct dp_point_cfg *cfg, struct dp_value *value,
                        const struct dp_value *old_value);
int idu_process_restore(uint16_t dev);
int idu_process_flush(uint16_t unused);
int idu_process_recover(uint16_t unused);

/* Profile-specific point handlers; selected by the product process table. */
int idu_mvio_seed_address(const struct dp_point_cfg *cfg, struct dp_value *value);
int idu_mvio_prepare_address(const struct dp_point_cfg *cfg, struct dp_value *value,
                             const struct dp_value *old);
int idu_aidc_process(struct dp_process_ctx *ctx, struct dp_key key);

#endif
