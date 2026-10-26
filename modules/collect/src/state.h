#ifndef CO_STATE_INTERNAL_H
#define CO_STATE_INTERNAL_H

#include <collect/collect.h>

struct co_outcome { bool replied, missing; };
struct co_schedule {
    bool managed;
    co_device_online_t state;
    size_t cursor;
    int64_t due;
    uint8_t retries;
    uint32_t replies;
};
/* 总线任务只执行此决策，不再解释设备状态。snapshot 是完成令牌，
 * 其中包含并发应答代次。 */
struct co_poll_plan {
    bool eligible, synchronous;
    int64_t due, deadline;
    uint8_t retries;
    struct co_schedule snapshot;
};
void co_state_bind(const struct dp_equip_cfg *cfg, uint8_t bus, bool simulated);
void co_state_arm(void);
void co_state_set_offline_callback(int (*callback)(uint16_t, int, int64_t));
int co_state_get(uint16_t dev, struct co_device_status *status);
void co_state_record(uint16_t dev, int error, struct co_outcome outcome);
void co_state_schedule(uint16_t dev, struct co_schedule *schedule);
void co_state_tick(void);
size_t co_state_next_init(uint16_t dev, size_t begin);
void co_state_poll_plan(size_t block, struct co_poll_plan *plan);
void co_state_poll_complete(size_t block, size_t rows, const struct co_poll_plan *plan,
                            int error, struct co_outcome outcome);
void co_state_poll_enable(size_t block, bool enabled);
bool co_state_poll_due(size_t block, int64_t now);
void co_state_initialized(uint16_t dev, int error);

#endif
