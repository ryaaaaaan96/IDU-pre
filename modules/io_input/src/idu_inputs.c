#include <io_input/idu_inputs.h>
#include <analog_input/analog_input.h>
#include <digital_io/digital_io.h>
#include <errno.h>

/* Optional IDU sampling profile; pins and order come from devicetree. */
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
BUILD_ASSERT(ARRAY_SIZE(analog_channels) == IDU_ANALOG_COUNT);
BUILD_ASSERT(ARRAY_SIZE(digital_pins) == IDU_DI_COUNT);
BUILD_ASSERT(ARRAY_SIZE(address_pins) == 4);
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) >= 0 &&
             DT_PROP(IO_NODE, address_offset) <= UINT8_MAX - 15);

static const struct analog_input_config idu_analog_inputs = {
    .channels = analog_channels, .channel_count = ARRAY_SIZE(analog_channels),
    .average_count = CONFIG_IDU_ADC_AVERAGE_COUNT,
};
static const struct digital_io_config idu_digital_inputs = {
    .pins = digital_pins, .count = ARRAY_SIZE(digital_pins),
    .order = DT_ENUM_IDX(IO_NODE, di_bit_order),
};
static const struct digital_io_config idu_address_inputs = {
    .pins = address_pins, .count = ARRAY_SIZE(address_pins),
    .order = DT_ENUM_IDX(IO_NODE, address_bit_order),
};
static const uint8_t idu_address_offset = DT_PROP(IO_NODE, address_offset);

int idu_inputs_init(void)
{
    /* Keep the existing engineering conversion's electrical contract. */
    for (size_t i = 0; i < idu_analog_inputs.channel_count; ++i) {
        const struct adc_dt_spec *channel = &idu_analog_inputs.channels[i];
        if (channel->vref_mv != 3300 || channel->resolution != 12 ||
            channel->channel_cfg.gain != ADC_GAIN_1 ||
            channel->channel_cfg.reference != ADC_REF_VDD_1 ||
            channel->channel_cfg.differential || channel->oversampling) {
            return -EINVAL;
        }
    }
    int err = digital_io_init(&idu_digital_inputs);
    if (!err) { err = digital_io_init(&idu_address_inputs); }
    return err ? err : analog_input_init(&idu_analog_inputs);
}

int idu_inputs_sample(struct idu_snapshot *snapshot)
{
    if (!snapshot) { return -EINVAL; }
    *snapshot = (struct idu_snapshot){0};
    uint32_t di, address;
    int err = digital_io_read(&idu_digital_inputs, &di);
    if (!err) { err = digital_io_read(&idu_address_inputs, &address); }
    if (!err) {
        snapshot->di = di;
        snapshot->address = address + idu_address_offset;
        err = analog_input_read(&idu_analog_inputs, snapshot->raw, ARRAY_SIZE(snapshot->raw));
    }
    snapshot->valid = !err;
    return err;
}
