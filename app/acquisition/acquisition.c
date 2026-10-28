#include "acquisition/acquisition.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>

#define IO_NODE DT_NODELABEL(idu_io)

/* 每路 AI 的硬件配置和目标测点直接放在同一行配置中。 */
struct analog_point {
    struct adc_dt_spec spec;
    uint16_t device_id;
    uint16_t signal_key;
};

static const struct analog_point analog_inputs[] = {
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_1),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_1,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_2),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_2,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_3),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_3,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_4),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_4,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_5),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_5,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_6),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_6,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_7),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_7,
    },
    {
        .spec = ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_8),
        .device_id = DEVICE_LOCAL,
        .signal_key = SIGNAL_CURRENT_8,
    },
};

/* 16 路 DI 合成为一个位图测点；不是每路 DI 分别写一次测点。 */
static const struct {
    struct gpio_dt_spec spec[SAMPLE_DI_COUNT];
    uint16_t device_id;
    uint16_t signal_key;
} digital_inputs = {
    .spec =
        {
            GPIO_DT_SPEC_GET(IO_NODE, di1_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di2_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di3_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di4_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di5_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di6_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di7_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di8_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di9_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di10_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di11_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di12_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di13_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di14_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di15_gpios),
            GPIO_DT_SPEC_GET(IO_NODE, di16_gpios),
        },
    .device_id = DEVICE_LOCAL,
    .signal_key = SIGNAL_DI,
};

int acquisition_inputs_init(void)
{
    /* 电流换算沿用 12 位、3.3 V、单端且无硬件过采样的配置。 */
    for (size_t i = 0; i < ARRAY_SIZE(analog_inputs); ++i) {
        const struct adc_dt_spec *spec = &analog_inputs[i].spec;
        if (!adc_is_ready_dt(spec)) {
            return -ENODEV;
        }
        if (spec->vref_mv != 3300) {
            return -EINVAL;
        }
        if (spec->resolution != 12) {
            return -EINVAL;
        }
        if (spec->channel_cfg.gain != ADC_GAIN_1) {
            return -EINVAL;
        }
        if (spec->channel_cfg.reference != ADC_REF_VDD_1) {
            return -EINVAL;
        }
        if (spec->channel_cfg.differential || spec->oversampling) {
            return -EINVAL;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs.spec); ++i) {
        if (!gpio_is_ready_dt(&digital_inputs.spec[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&digital_inputs.spec[i], GPIO_INPUT);
        if (err) {
            return err;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(analog_inputs); ++i) {
        int err = adc_channel_setup_dt(&analog_inputs[i].spec);
        if (err) {
            return err;
        }
    }
    return 0;
}

int acquisition_cycle(void)
{
    uint16_t digital = 0;
    uint8_t bytes[2];

    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs.spec); ++i) {
        int value = gpio_pin_get_dt(&digital_inputs.spec[i]);
        if (value < 0) {
            return value;
        }
        size_t bit =
            DT_ENUM_IDX(IO_NODE, di_bit_order) == 0 ? i : ARRAY_SIZE(digital_inputs.spec) - 1 - i;
        digital |= (uint16_t)((uint32_t)value << bit);
    }
    sys_put_le16(digital, bytes);
    int err = dp_set_para(digital_inputs.device_id, digital_inputs.signal_key, sizeof(bytes), bytes);
    if (err) {
        return err;
    }

    for (size_t i = 0; i < ARRAY_SIZE(analog_inputs); ++i) {
        const struct analog_point *input = &analog_inputs[i];
        uint16_t raw;
        uint32_t sum = 0;
        struct adc_sequence sequence = {
            .buffer = &raw,
            .buffer_size = sizeof(raw),
        };
        err = adc_sequence_init_dt(&input->spec, &sequence);
        if (err) {
            return err;
        }
        for (uint32_t n = 0; n < CONFIG_IDU_ADC_AVERAGE_COUNT; ++n) {
            err = adc_read_dt(&input->spec, &sequence);
            if (err) {
                return err;
            }
            sum += raw;
        }
        /* 原电流公式：raw / 4096 * 3.3 / 0.24 * 50 * 100，超限饱和。 */
        uint32_t value = (sum / CONFIG_IDU_ADC_AVERAGE_COUNT) * 68750U / 4096U;
        if (value > UINT16_MAX) {
            value = UINT16_MAX;
        }
        sys_put_le16(value, bytes);
        err = dp_set_para(input->device_id, input->signal_key, sizeof(bytes), bytes);
        if (err) {
            return err;
        }
    }
    return 0;
}
