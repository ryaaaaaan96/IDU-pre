/* Bounded, sequential E50x sampling. DMA migration can be added separately.
 * Upstream v4.4.2 has no E50x ADC DT node/complete ADC trigger setup.
 */
#include "hardware/idu_pre.h"
#include <errno.h>
#include <gd32_adc.h>
#include <gd32_rcu.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define ADC_NODE DT_NODELABEL(idu_analog)
#define IDU_ADC_REG DT_REG_ADDR(ADC_NODE)
PINCTRL_DT_DEFINE(ADC_NODE);
static const uint8_t channel_order[] = DT_PROP(ADC_NODE, channel_order);
BUILD_ASSERT(ARRAY_SIZE(channel_order) == IDU_ANALOG_COUNT);
BUILD_ASSERT(DT_PROP(ADC_NODE, vref_mv) == 3300,
             "Legacy current conversion assumes a 3.3 V ADC reference");

static int wait_clear(uint32_t mask)
{
    int64_t deadline = k_uptime_get() + 10;
    while (ADC_CTL1(IDU_ADC_REG) & mask) {
        if (k_uptime_get() >= deadline) {
            return -ETIMEDOUT;
        }
    }
    return 0;
}

int idu_analog_init(void)
{
    int err = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(ADC_NODE),
                                  PINCTRL_STATE_DEFAULT);
    if (err) {
        return err;
    }
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
    adc_deinit(IDU_ADC_REG);
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(IDU_ADC_REG, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(IDU_ADC_REG, ADC_SCAN_MODE, DISABLE);
    adc_data_alignment_config(IDU_ADC_REG, ADC_DATAALIGN_RIGHT);
    adc_resolution_config(IDU_ADC_REG, ADC_RESOLUTION_12B);
    adc_channel_length_config(IDU_ADC_REG, ADC_REGULAR_CHANNEL, 1);
    adc_external_trigger_source_config(IDU_ADC_REG, ADC_REGULAR_CHANNEL,
                                       ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(IDU_ADC_REG, ADC_REGULAR_CHANNEL, ENABLE);
    adc_enable(IDU_ADC_REG);
    k_busy_wait(10);
    ADC_CTL1(IDU_ADC_REG) |= ADC_CTL1_RSTCLB;
    err = wait_clear(ADC_CTL1_RSTCLB);
    if (!err) {
        ADC_CTL1(IDU_ADC_REG) |= ADC_CTL1_CLB;
        err = wait_clear(ADC_CTL1_CLB);
    }
    return err;
}

int idu_analog_read(uint16_t raw[IDU_ANALOG_COUNT])
{
    for (size_t i = 0; i < ARRAY_SIZE(channel_order); ++i) {
        uint32_t sum = 0;
        adc_regular_channel_config(IDU_ADC_REG, 0, channel_order[i],
                                    ADC_SAMPLETIME_71POINT5);
        for (int sample = 0; sample < CONFIG_IDU_ADC_AVERAGE_COUNT; ++sample) {
            adc_flag_clear(IDU_ADC_REG, ADC_FLAG_EOC);
            adc_software_trigger_enable(IDU_ADC_REG, ADC_REGULAR_CHANNEL);
            int64_t deadline = k_uptime_get() + 2;
            while (!adc_flag_get(IDU_ADC_REG, ADC_FLAG_EOC)) {
                if (k_uptime_get() >= deadline) {
                    return -ETIMEDOUT;
                }
            }
            sum += adc_regular_data_read(IDU_ADC_REG);
        }
        raw[i] = sum / CONFIG_IDU_ADC_AVERAGE_COUNT;
    }
    return 0;
}
