#ifndef IDU_ENGINEERING_H
#define IDU_ENGINEERING_H

#include <stdint.h>

/* Convert a raw ADC value to the legacy wire unit (0.01 A), saturating at U16. */
uint16_t idu_current_from_raw(uint16_t raw);

#endif
