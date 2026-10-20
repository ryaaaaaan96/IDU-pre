/* Product channel ordering and averaging; hardware access uses Zephyr ADC. */
#include "hardware/idu_pre.h"
#include <errno.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>

#define IO_NODE DT_NODELABEL(idu_io)
#define CHANNEL_SPEC(node, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node, idx),
static const struct adc_dt_spec channels[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, io_channels, CHANNEL_SPEC)
};
BUILD_ASSERT(ARRAY_SIZE(channels) == IDU_ANALOG_COUNT);

int idu_analog_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        if (!adc_is_ready_dt(&channels[i])) {
            return -ENODEV;
        }
        /* Existing engineering conversion expects a 12-bit, 3.3 V sample. */
        if (channels[i].vref_mv != 3300 || channels[i].resolution != 12 ||
            channels[i].channel_cfg.gain != ADC_GAIN_1 ||
            channels[i].channel_cfg.reference != ADC_REF_VDD_1 ||
            channels[i].channel_cfg.differential || channels[i].oversampling) {
            return -EINVAL;
        }
        int err = adc_channel_setup_dt(&channels[i]);
        if (err) {
            return err;
        }
    }
    return 0;
}

int idu_analog_read(uint16_t raw[IDU_ANALOG_COUNT])
{
    for (size_t i = 0; i < ARRAY_SIZE(channels); ++i) {
        uint16_t sample;
        uint32_t sum = 0;
        struct adc_sequence seq = {.buffer = &sample, .buffer_size = sizeof(sample)};
        int err = adc_sequence_init_dt(&channels[i], &seq);

        if (err) {
            return err;
        }
        for (int count = 0; count < CONFIG_IDU_ADC_AVERAGE_COUNT; ++count) {
            err = adc_read_dt(&channels[i], &seq);
            if (err) {
                return err;
            }
            sum += sample;
        }
        raw[i] = sum / CONFIG_IDU_ADC_AVERAGE_COUNT;
    }
    return 0;
}
