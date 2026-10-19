#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <dataprocess/points.h>

int idu_process_current(const struct dp_point_cfg *cfg, struct dp_value *value,
                        const struct dp_value *old_value);
uint16_t idu_sample_period_ms(void);
int idu_process_restore(uint16_t dev);
int idu_process_flush(uint16_t unused);
int idu_process_recover(uint16_t unused);

#endif
