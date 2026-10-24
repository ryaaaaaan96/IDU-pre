#ifndef DP_ACQUISITION_H
#define DP_ACQUISITION_H

#include <dataprocess/points.h>

/* 可直接登记的采集消费者，均不负责配置 CO。 */
int dp_accept_samples(struct dp_point_batch *batch, bool sync);
int dp_initialize_acquired_device(uint16_t dev_id);

/* 保留缓存值和持久目标，只将采集键标为无效。
 * observed_at 是转为离线的时间，较早排队的样本仍保持无效。
 */
int dp_invalidate_acquired_device(uint16_t dev_id, int error, int64_t observed_at);

#endif
