#ifndef DP_ACQUISITION_H
#define DP_ACQUISITION_H

#include <dataprocess/points.h>

/* Ready-to-register acquisition consumers; none configures CO. */
int dp_accept_samples(struct dp_point_batch *batch, bool sync);
int dp_initialize_acquired_device(uint16_t dev_id);

/* Retain cached values and durable targets; invalidate acquired keys only.
 * observed_at is the offline transition time; older queued samples stay invalid.
 */
int dp_invalidate_acquired_device(uint16_t dev_id, int error, int64_t observed_at);

#endif
