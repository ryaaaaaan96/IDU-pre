/* Legacy engineering conversion, shared by process callbacks and tests. */
#include "idu_pre.h"

uint16_t idu_current_from_raw(uint16_t raw)
{
    /* Legacy: raw / 4096 * 3.3 / 0.24 * 50 * 100.
     * Saturate values exceeding the 16-bit register instead of wrapping.
     */
    uint32_t value = (uint32_t)raw * 68750U / 4096U;
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}
