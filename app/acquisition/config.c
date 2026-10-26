#include "acquisition/sample.h"
#include <input_sampler/input_sampler.h>
#include <errno.h>

/* 产品硬件选择集中在此处；通用模块只接收描述信息。 */
#define IO_NODE DT_NODELABEL(idu_io)
#define ADC_SPEC(node, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node, idx),
#define GPIO_SPEC(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),

static const struct adc_dt_spec analog_channels[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, io_channels, ADC_SPEC)
};
static const struct gpio_dt_spec digital_pins[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, di_gpios, GPIO_SPEC)
};
static const struct gpio_dt_spec address_pins[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, address_gpios, GPIO_SPEC)
};
BUILD_ASSERT(ARRAY_SIZE(analog_channels) == SAMPLE_ANALOG_COUNT);
BUILD_ASSERT(ARRAY_SIZE(digital_pins) == SAMPLE_DI_COUNT);
BUILD_ASSERT(ARRAY_SIZE(address_pins) == 4);
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) >= 0 &&
             DT_PROP(IO_NODE, address_offset) <= UINT8_MAX - 15);

static const struct analog_input_config analog_inputs = {
    .channels = analog_channels,
    .channel_count = ARRAY_SIZE(analog_channels),
    .average_count = CONFIG_IDU_ADC_AVERAGE_COUNT,
};
typedef enum { DIGITAL_INPUTS, ADDRESS_INPUTS, DIGITAL_GROUP_COUNT } digital_group_t;
static const struct digital_io_config digital_groups[] = {
    [DIGITAL_INPUTS] = {
        .pins = digital_pins,
        .count = ARRAY_SIZE(digital_pins),
        .order = DT_ENUM_IDX(IO_NODE, di_bit_order),
    },
    [ADDRESS_INPUTS] = {
        .pins = address_pins,
        .count = ARRAY_SIZE(address_pins),
        .order = DT_ENUM_IDX(IO_NODE, address_bit_order),
    },
};
static const struct input_sampler_config inputs = {
    .analog = &analog_inputs,
    .digital_groups = digital_groups,
    .digital_group_count = ARRAY_SIZE(digital_groups),
};

int acquisition_inputs_init(void)
{
    /* 这些电气假设属于产品的电流换算规则。 */
    for (size_t i = 0; i < ARRAY_SIZE(analog_channels); ++i) {
        const struct adc_dt_spec *channel = &analog_channels[i];
        if (channel->vref_mv != 3300 || channel->resolution != 12 ||
            channel->channel_cfg.gain != ADC_GAIN_1 ||
            channel->channel_cfg.reference != ADC_REF_VDD_1 ||
            channel->channel_cfg.differential || channel->oversampling) {
            return -EINVAL;
        }
    }
    return input_sampler_init(&inputs);
}

int acquisition_inputs_sample(struct acquisition_sample *sample)
{
    if (!sample) { return -EINVAL; }
    *sample = (struct acquisition_sample){0};
    uint32_t digital[DIGITAL_GROUP_COUNT] = {0};
    struct input_sampler_sample output = {
        .analog = sample->raw,
        .analog_capacity = ARRAY_SIZE(sample->raw),
        .digital = digital,
        .digital_capacity = ARRAY_SIZE(digital),
        .valid = false,
    };
    int err = input_sampler_read(&inputs, &output);

    if (!err) {
        sample->di = digital[DIGITAL_INPUTS];
        sample->address = digital[ADDRESS_INPUTS] + DT_PROP(IO_NODE, address_offset);
    }
    sample->valid = output.valid;
    return err;
}
