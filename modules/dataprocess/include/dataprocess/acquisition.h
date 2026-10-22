#ifndef DP_ACQUISITION_H
#define DP_ACQUISITION_H

#include <dataprocess/points.h>

/* Ready-to-register acquisition consumers; neither function configures CO. */
int dp_accept_samples(struct dp_point_batch *batch, bool sync);
int dp_initialize_acquired_device(uint16_t dev_id);

#endif
