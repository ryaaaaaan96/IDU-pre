#include "acquisition/acquisition.h"
#include "protocol/inc/protocol.h"
#include "process/points.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_acquisition, LOG_LEVEL_INF);

/* 按名称选择设备树输入，属性排列顺序不决定产品样本顺序。
 * 修改硬件名称时，同步修改设备树名称与这里的对应引用。
 */
#define IO_NODE DT_NODELABEL(idu_io)

static const struct adc_dt_spec analog_channels[] = {
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_1),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_2),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_3),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_4),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_5),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_6),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_7),
    ADC_DT_SPEC_GET_BY_NAME(IO_NODE, current_8),
};

static const struct gpio_dt_spec digital_pins[] = {
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
};

static const struct gpio_dt_spec address_pins[] = {
    GPIO_DT_SPEC_GET(IO_NODE, address1_gpios),
    GPIO_DT_SPEC_GET(IO_NODE, address2_gpios),
    GPIO_DT_SPEC_GET(IO_NODE, address3_gpios),
    GPIO_DT_SPEC_GET(IO_NODE, address4_gpios),
};

BUILD_ASSERT(ARRAY_SIZE(analog_channels) == SAMPLE_ANALOG_COUNT,
             "Named ADC inputs must match the product sample layout");
BUILD_ASSERT(ARRAY_SIZE(digital_pins) == SAMPLE_DI_COUNT,
             "Named digital inputs must match the product sample layout");
BUILD_ASSERT(ARRAY_SIZE(address_pins) == 4,
             "Address selector requires four named GPIO inputs");
/* 四位拨码取值为 0 至 15，加偏移后必须能存入 uint8_t 地址。 */
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) >= 0,
             "Address offset must not be negative");
BUILD_ASSERT(DT_PROP(IO_NODE, address_offset) <= UINT8_MAX - 15,
             "Address offset plus the four-bit selector must fit in uint8_t");

int acquisition_inputs_init(void)
{
    /* 校验电流换算所要求的 ADC 电气参数。 */
    for (size_t i = 0; i < ARRAY_SIZE(analog_channels); ++i) {
        const struct adc_dt_spec *channel = &analog_channels[i];

        if (!adc_is_ready_dt(channel)) {
            return -ENODEV;
        }
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
    for (size_t i = 0; i < ARRAY_SIZE(digital_pins); ++i) {
        if (!gpio_is_ready_dt(&digital_pins[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&digital_pins[i], GPIO_INPUT);
        if (err) {
            return err;
        }
    }

    /* 初始化拨码地址输入。 */
    for (size_t i = 0; i < ARRAY_SIZE(address_pins); ++i) {
        if (!gpio_is_ready_dt(&address_pins[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&address_pins[i], GPIO_INPUT);
        if (err) {
            return err;
        }
    }

    /* 初始化 ADC 输入通道。 */
    for (size_t i = 0; i < ARRAY_SIZE(analog_channels); ++i) {
        int err = adc_channel_setup_dt(&analog_channels[i]);
        if (err) {
            return err;
        }
    }

    return 0;
}

int acquisition_inputs_sample(struct acquisition_sample *sample)
{
    if (!sample) { return -EINVAL; }
    *sample = (struct acquisition_sample){0};
    uint16_t digital = 0;
    uint8_t address = 0;

    /* 绑定中的 lsb-first 枚举值为 0，msb-first 为 1。 */
    for (size_t i = 0; i < ARRAY_SIZE(digital_pins); ++i) {
        int value = gpio_pin_get_dt(&digital_pins[i]);
        if (value < 0) {
            return value;
        }
        size_t bit = DT_ENUM_IDX(IO_NODE, di_bit_order) == 0
                   ? i : ARRAY_SIZE(digital_pins) - 1 - i;
        digital |= (uint16_t)((uint32_t)value << bit);
    }

    for (size_t i = 0; i < ARRAY_SIZE(address_pins); ++i) {
        int value = gpio_pin_get_dt(&address_pins[i]);
        if (value < 0) {
            return value;
        }
        size_t bit = DT_ENUM_IDX(IO_NODE, address_bit_order) == 0
                   ? i : ARRAY_SIZE(address_pins) - 1 - i;
        address |= (uint8_t)((uint32_t)value << bit);
    }

    for (size_t i = 0; i < ARRAY_SIZE(analog_channels); ++i) {
        uint16_t raw;
        uint32_t sum = 0;
        struct adc_sequence sequence = {
            .buffer = &raw,
            .buffer_size = sizeof(raw),
        };

        int err = adc_sequence_init_dt(&analog_channels[i], &sequence);
        if (err) {
            return err;
        }
        for (uint32_t n = 0; n < CONFIG_IDU_ADC_AVERAGE_COUNT; ++n) {
            err = adc_read_dt(&analog_channels[i], &sequence);
            if (err) {
                return err;
            }
            sum += raw;
        }
        sample->raw[i] = sum / CONFIG_IDU_ADC_AVERAGE_COUNT;
    }

    sample->di = digital;
    sample->address = address + DT_PROP(IO_NODE, address_offset);
    sample->valid = true;
    return 0;
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
