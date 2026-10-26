#include "acquisition/acquisition.h"
#include "protocol/inc/protocol.h"
#include "process/points.h"
#include <analog_io.h>
#include <gpio/digital_io.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_acquisition, LOG_LEVEL_INF);

/* 下列数字是设备树属性的元素索引，不是 ADC 通道号或 GPIO 引脚号。
 * 数组左侧是产品样本顺序，右侧可独立选择设备树中的元素。
 * 实际引脚、通道号和极性由设备树提供，不要求硬件编号连续。
 */
#define IO_NODE DT_NODELABEL(idu_io)

static const struct adc_dt_spec analog_channels[] = {
    [0] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 0), /* 模拟量输入 1。 */
    [1] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 1), /* 模拟量输入 2。 */
    [2] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 2), /* 模拟量输入 3。 */
    [3] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 3), /* 模拟量输入 4。 */
    [4] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 4), /* 模拟量输入 5。 */
    [5] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 5), /* 模拟量输入 6。 */
    [6] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 6), /* 模拟量输入 7。 */
    [7] = ADC_DT_SPEC_GET_BY_IDX(IO_NODE, 7), /* 模拟量输入 8。 */
};

static const struct gpio_dt_spec digital_pins[] = {
    [0] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 0), /* DI1。 */
    [1] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 1), /* DI2。 */
    [2] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 2), /* DI3。 */
    [3] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 3), /* DI4。 */
    [4] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 4), /* DI5。 */
    [5] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 5), /* DI6。 */
    [6] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 6), /* DI7。 */
    [7] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 7), /* DI8。 */
    [8] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 8), /* DI9。 */
    [9] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 9), /* DI10。 */
    [10] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 10), /* DI11。 */
    [11] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 11), /* DI12。 */
    [12] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 12), /* DI13。 */
    [13] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 13), /* DI14。 */
    [14] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 14), /* DI15。 */
    [15] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, di_gpios, 15), /* DI16。 */
};

static const struct gpio_dt_spec address_pins[] = {
    [0] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, address_gpios, 0), /* 第 1 路拨码输入。 */
    [1] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, address_gpios, 1), /* 第 2 路拨码输入。 */
    [2] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, address_gpios, 2), /* 第 3 路拨码输入。 */
    [3] = GPIO_DT_SPEC_GET_BY_IDX(IO_NODE, address_gpios, 3), /* 第 4 路拨码输入。 */
};

/* 显式数组不再自动跟随设备树长度，分别检查设备树和产品样本布局。 */
BUILD_ASSERT(DT_PROP_LEN(IO_NODE, io_channels) == ARRAY_SIZE(analog_channels),
             "ADC devicetree entries must match the explicit channel mapping");
BUILD_ASSERT(ARRAY_SIZE(analog_channels) == SAMPLE_ANALOG_COUNT,
             "ADC mapping must match the product sample layout");
BUILD_ASSERT(DT_PROP_LEN(IO_NODE, di_gpios) == ARRAY_SIZE(digital_pins),
             "DI devicetree entries must match the explicit pin mapping");
BUILD_ASSERT(ARRAY_SIZE(digital_pins) == SAMPLE_DI_COUNT,
             "DI mapping must match the product sample layout");
BUILD_ASSERT(DT_PROP_LEN(IO_NODE, address_gpios) == ARRAY_SIZE(address_pins),
             "Address devicetree entries must match the explicit pin mapping");
BUILD_ASSERT(ARRAY_SIZE(address_pins) == 4,
             "Address selector requires four GPIO inputs");
/* 四位拨码取值为 0 至 15，加偏移后必须能存入 uint8_t 地址。 */
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) >= 0,
             "Address offset must not be negative");
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) <= UINT8_MAX - 15,
             "Address offset plus the four-bit selector must fit in uint8_t");

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
