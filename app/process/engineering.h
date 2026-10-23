#ifndef ENGINEERING_H
#define ENGINEERING_H

#include <stdint.h>

/* Convert a raw ADC value to the wire unit (0.01 A), saturating at U16. */
uint16_t engineering_current_from_raw(uint16_t raw);

#endif
