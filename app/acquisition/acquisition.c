#include "acquisition/acquisition.h"
#include "protocol/inc/protocol.h"
#include "process/points.h"
#include <analog_io.h>
#include <gpio/digital_io.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_acquisition, LOG_LEVEL_INF);

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

static const struct analog_adc_config analog_inputs = {
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
int acquisition_inputs_init(void)
{
    /* 校验电流换算所要求的 ADC 电气参数。 */
    for (size_t i = 0; i < ARRAY_SIZE(analog_channels); ++i) {
        const struct adc_dt_spec *channel = &analog_channels[i];

        if (channel->vref_mv != 3300) {
            return -EINVAL;
        }
        if (channel->resolution != 12) {
            return -EINVAL;
        }
        if (channel->channel_cfg.gain != ADC_GAIN_1) {
            return -EINVAL;
        }
        if (channel->channel_cfg.reference != ADC_REF_VDD_1) {
            return -EINVAL;
        }
        if (channel->channel_cfg.differential) {
            return -EINVAL;
        }
        if (channel->oversampling) {
            return -EINVAL;
        }
    }

    /* 初始化 DI 输入。 */
    int err = digital_io_init(&digital_groups[DIGITAL_INPUTS]);
    if (err) {
        return err;
    }

    /* 初始化拨码地址输入。 */
    err = digital_io_init(&digital_groups[ADDRESS_INPUTS]);
    if (err) {
        return err;
    }

    /* 初始化 ADC 输入通道。 */
    err = analog_adc_init(&analog_inputs);
    if (err) {
        return err;
    }

    return 0;
}

int acquisition_inputs_sample(struct acquisition_sample *sample)
{
    if (!sample) { return -EINVAL; }
    *sample = (struct acquisition_sample){0};
    uint32_t digital[DIGITAL_GROUP_COUNT] = {0};
    for (size_t i = 0; i < ARRAY_SIZE(digital_groups); ++i) {
        int err = digital_io_read(&digital_groups[i], &digital[i]);
        if (err) { return err; }
    }
    int err = analog_adc_read(&analog_inputs, sample->raw, ARRAY_SIZE(sample->raw));

    if (!err) {
        sample->di = digital[DIGITAL_INPUTS];
        sample->address = digital[ADDRESS_INPUTS] + DT_PROP(IO_NODE, address_offset);
    }
    sample->valid = !err;
    return err;
}

int acquisition_cycle(void)
{
    if (status_phase_get() != STATUS_RUN) {
        return -EACCES;
    }

    status_set_phase(STATUS_SAMPLING);
    struct acquisition_sample sample = {0};
    int err = acquisition_inputs_sample(&sample);

    sample.valid = !err;

    /* 业务层负责将数值映射到 DP。先发布无效质量；若发布也失败，
     * 仍保留采样失败作为故障来源。 */
    status_set_phase(STATUS_PUBLISHING);
    int publish_err = points_publish_snapshot(&sample);
    if (err) {
        return status_report_fault(STATUS_RUNTIME, err, "Input sampling failed",
                                   STATUS_ACTION_RESET);
    }
    if (publish_err) {
        return status_report_fault(STATUS_RUNTIME, publish_err, "Sample publication failed",
                                   STATUS_ACTION_RESET);
    }

    status_set_phase(STATUS_SERVICING_COMMUNICATION);
    protocol_update_status();
    err = protocol_error();
    if (err) {
        return status_report_fault(STATUS_RUNTIME, err, "Protocol maintenance failed",
                                   STATUS_ACTION_RESET);
    }

    status_set_phase(STATUS_CHECKING_HEALTH);
    err = status_progress();
    if (err) {
        return err; /* status_progress 已保留准确的故障来源。 */
    }
    status_set_phase(STATUS_RUN);
    LOG_DBG("Address=%u DI=0x%04x", sample.address, sample.di);
    return 0;
}
