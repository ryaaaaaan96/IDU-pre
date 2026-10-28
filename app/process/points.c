/* 应用采样周期读取；硬件输入映射归 acquisition。 */
#include "process/points.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>

uint16_t points_sample_period_ms(void)
{
    value_t value;
    if (dp_get_para(DEVICE_LOCAL, SIGNAL_SAMPLE_PERIOD, &value) < 0) {
        return CONFIG_IDU_SAMPLE_PERIOD_MS;
    }
    return value.u16;
}
