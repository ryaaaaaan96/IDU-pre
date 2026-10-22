#ifndef CO_STATE_INTERNAL_H
#define CO_STATE_INTERNAL_H

#include <collect/collect.h>

struct co_outcome { bool replied, missing; };
struct co_schedule {
    bool managed;
    enum co_device_online state;
    size_t cursor;
    int64_t due;
    uint8_t retries;
    uint32_t replies;
};
void co_state_bind(const struct dp_equip_cfg *cfg, uint8_t bus, bool simulated);
void co_state_arm(void);
int co_state_get(uint16_t dev, struct co_device_status *status);
void co_state_record(uint16_t dev, int error, struct co_outcome outcome);
void co_state_schedule(uint16_t dev, struct co_schedule *schedule);
void co_state_tick(void);
size_t co_state_next_init(uint16_t dev, size_t begin);
void co_state_complete(uint16_t dev, const struct co_schedule *before,
                       size_t first, size_t rows, int error, struct co_outcome outcome);
void co_state_initialized(uint16_t dev, int error);

#endif
