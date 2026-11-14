/* TPC5120 同步 ADC 后端；协议依据 3PEAK BA20241107A1 第 11–16 页。 */
#define DT_DRV_COMPAT threepeak_tpc5120

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define TPC5120_CHANNELS 16U
#define TPC5120_MANUAL (BIT(12) | BIT(11))
#define TPC5120_RANGE_2X BIT(6)

BUILD_ASSERT(CONFIG_ADC_TPC5120_INIT_PRIORITY > CONFIG_SPI_INIT_PRIORITY,
    "TPC5120 must initialize after its SPI controller");

struct tpc5120_config {
    struct spi_dt_spec spi;
};

struct tpc5120_data {
    struct k_mutex lock;
    uint16_t configured;
    uint16_t range_2x;
};

static int tpc5120_channel_setup(const struct device *dev, const struct adc_channel_cfg *channel)
{
    struct tpc5120_data *data = dev->data;

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (channel->channel_id >= TPC5120_CHANNELS) {
        return -EINVAL;
    }
    if ((channel->gain != ADC_GAIN_1 && channel->gain != ADC_GAIN_1_2) ||
        channel->reference != ADC_REF_EXTERNAL0 || channel->differential ||
        channel->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
        return -ENOTSUP;
    }
    k_mutex_lock(&data->lock, K_FOREVER);
    if (channel->gain == ADC_GAIN_1_2) {
        data->range_2x |= BIT(channel->channel_id);
    } else {
        data->range_2x &= ~BIT(channel->channel_id);
    }
    data->configured |= BIT(channel->channel_id);
    k_mutex_unlock(&data->lock);
    return 0;
}

/* 两个 8 位字节构成一帧，显式 MSB 编码，不依赖 MCU 字节序或 16 位 SPI 支持。 */
static int tpc5120_frame(const struct tpc5120_config *cfg, uint16_t command, uint16_t *result)
{
    uint8_t tx[2];
    uint8_t rx[2];
    struct spi_buf tx_buf = {.buf = tx, .len = sizeof(tx)};
    struct spi_buf rx_buf = {.buf = rx, .len = sizeof(rx)};
    const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    sys_put_be16(command, tx);
    int err = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);

    /* CS 高电平保持至少 1 us，覆盖手册的总线释放和安静采样时间。 */
    k_busy_wait(1);
    if (!err) {
        *result = sys_get_be16(rx);
    }
    return err;
}

static int tpc5120_read(const struct device *dev, const struct adc_sequence *sequence)
{
    const struct tpc5120_config *cfg = dev->config;
    struct tpc5120_data *data = dev->data;
    uint8_t channels[TPC5120_CHANNELS];
    uint16_t samples[TPC5120_CHANNELS];
    unsigned int count = 0;
    int err = 0;

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!sequence->channels || (sequence->channels & ~0xffffU) || !sequence->buffer ||
        (uintptr_t)sequence->buffer % sizeof(uint16_t)) {
        return -EINVAL;
    }
    if (sequence->resolution != 12 || sequence->oversampling || sequence->calibrate ||
        sequence->options) {
        return -ENOTSUP;
    }
    for (unsigned int ch = 0; ch < TPC5120_CHANNELS; ++ch) {
        if (sequence->channels & BIT(ch)) {
            channels[count++] = ch;
        }
    }
    if (sequence->buffer_size < count * sizeof(uint16_t)) {
        return -ENOMEM;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    if ((data->configured & sequence->channels) != sequence->channels) {
        err = -EINVAL;
        goto out;
    }
    /* N 帧写入通道和量程，在 N+2 帧得到结果；每批丢弃前两帧以重建流水线。 */
    for (unsigned int frame = 0; frame < count + 2U; ++frame) {
        unsigned int ch = channels[MIN(frame, count - 1U)];
        uint16_t command = TPC5120_MANUAL | (ch << 7);
        uint16_t result;

        if (data->range_2x & BIT(ch)) {
            command |= TPC5120_RANGE_2X;
        }
        err = tpc5120_frame(cfg, command, &result);
        if (err) {
            goto out;
        }
        if (frame >= 2U) {
            if ((result >> 12) != channels[frame - 2U]) {
                err = -EIO;
                goto out;
            }
            samples[frame - 2U] = result & 0x0fffU;
        }
    }
    /* 整批通过 SPI 和通道头校验后才提交，失败时调用方缓冲区保持原值。 */
    memcpy(sequence->buffer, samples, count * sizeof(uint16_t));
out:
    k_mutex_unlock(&data->lock);
    return err;
}

#ifdef CONFIG_ADC_ASYNC
static int tpc5120_read_async(
    const struct device *dev, const struct adc_sequence *sequence, struct k_poll_signal *signal)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(sequence);
    ARG_UNUSED(signal);
    return -ENOTSUP;
}
#endif

static DEVICE_API(adc, tpc5120_api) = {
    .channel_setup = tpc5120_channel_setup,
    .read = tpc5120_read,
#ifdef CONFIG_ADC_ASYNC
    .read_async = tpc5120_read_async,
#endif
};

static int tpc5120_init(const struct device *dev)
{
    const struct tpc5120_config *cfg = dev->config;
    struct tpc5120_data *data = dev->data;

    if (!spi_is_ready_dt(&cfg->spi)) {
        return -ENODEV;
    }
    /* 不允许 DTS 静默覆盖协议要求；CS 必须在每个 16 时钟帧间释放。 */
    if (cfg->spi.config.frequency == 0 || cfg->spi.config.frequency > 20000000U ||
        (cfg->spi.config.operation &
            (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB | SPI_HOLD_ON_CS | SPI_LOCK_ON |
                SPI_CS_ACTIVE_HIGH | SPI_HALF_DUPLEX | SPI_FRAME_FORMAT_TI))) {
        return -EINVAL;
    }
    k_mutex_init(&data->lock);
    k_busy_wait(1);
    return 0;
}

#define TPC5120_DEFINE(n)                                                                          \
    static struct tpc5120_data tpc5120_data_##n;                                                   \
    static const struct tpc5120_config tpc5120_config_##n = {                                      \
        .spi = SPI_DT_SPEC_INST_GET(n, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                        \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n,                                                                       \
        tpc5120_init,                                                                              \
        NULL,                                                                                      \
        &tpc5120_data_##n,                                                                         \
        &tpc5120_config_##n,                                                                       \
        POST_KERNEL,                                                                               \
        CONFIG_ADC_TPC5120_INIT_PRIORITY,                                                          \
        &tpc5120_api);

DT_INST_FOREACH_STATUS_OKAY(TPC5120_DEFINE)
