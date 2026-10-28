/* 应用采样周期读取；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>

uint16_t points_sample_period_ms(void)
{
    uint8_t bytes[2];
    if (dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, sizeof(bytes), bytes) < 0) {
        return CONFIG_IDU_SAMPLE_PERIOD_MS;
    }
    return bytes[0] | ((uint16_t)bytes[1] << 8);
}
