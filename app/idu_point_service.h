#ifndef IDU_POINT_SERVICE_H
#define IDU_POINT_SERVICE_H

#include <stdint.h>

struct idu_snapshot;

/* Product assembly and sample mapping; call from application-thread context. */
int idu_equipment_init(void);
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);

#endif
