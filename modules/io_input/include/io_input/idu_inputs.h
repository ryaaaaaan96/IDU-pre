#ifndef IDU_INPUTS_H
#define IDU_INPUTS_H

#include <stdbool.h>
#include <stdint.h>

#define IDU_ANALOG_COUNT 8
#define IDU_DI_COUNT 16

struct idu_snapshot {
    uint16_t di;
    uint16_t raw[IDU_ANALOG_COUNT];
    uint16_t current[IDU_ANALOG_COUNT]; /* Legacy wire unit: 0.01 A */
    uint8_t address;
    bool valid;
};

int idu_inputs_init(void);
/* Unified product sampling entry: logical DI/address and raw ADC, no DP writes.
 * On failure the snapshot is marked invalid; partial values must not be used.
 */
int idu_inputs_sample(struct idu_snapshot *snapshot);

#endif
