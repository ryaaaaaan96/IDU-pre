/* 沿用原有工程量换算，由处理回调和测试共用。 */
#include "process/engineering.h"

uint16_t engineering_current_from_raw(uint16_t raw)
{
    /* 原有公式：
     * raw / 4096 * 3.3 / 0.24 * 50 * 100。
     * 超出 16 位寄存器范围时饱和处理，避免回绕。
     */
    uint32_t value = (uint32_t)raw * 68750U / 4096U;
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}
