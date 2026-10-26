#include <analog_io.h>
#include <errno.h>

static int validate(const struct analog_adc_config *config)
{
    if (!config || !config->channels || !config->channel_count || !config->average_count) {
        return -EINVAL;
    }
    for (size_t i = 0; i < config->channel_count; ++i) {
        const struct adc_dt_spec *channel = &config->channels[i];
        if (!channel->dev || !adc_is_ready_dt(channel)) { return -ENODEV; }
        if (!channel->channel_cfg_dt_node_exists || channel->channel_id >= 32) {
            return -EINVAL;
        }
        if (channel->channel_cfg.differential || !channel->resolution ||
            channel->resolution > 16) { return -ENOTSUP; }
    }
    return 0;
}

int analog_adc_init(const struct analog_adc_config *config)
{
    int err = validate(config);
    if (err) { return err; }
    for (size_t i = 0; i < config->channel_count; ++i) {
        err = adc_channel_setup_dt(&config->channels[i]);
        if (err) { return err; }
    }
    return 0;
}

int analog_adc_read(const struct analog_adc_config *config,
                      uint16_t *values, size_t capacity)
{
    if (!values) { return -EINVAL; }
    int err = validate(config);
    if (err) { return err; }
    if (capacity < config->channel_count) { return -ENOMEM; }
    for (size_t i = 0; i < config->channel_count; ++i) {
        uint16_t sample;
        uint32_t sum = 0;
        struct adc_sequence sequence = {.buffer = &sample, .buffer_size = sizeof(sample)};
        err = adc_sequence_init_dt(&config->channels[i], &sequence);
        if (err) { return err; }
        for (uint32_t n = 0; n < config->average_count; ++n) {
            err = adc_read_dt(&config->channels[i], &sequence);
            if (err) { return err; }
            sum += sample; /* UINT16_MAX 个 UINT16_MAX 样本的总和可存入 uint32_t。 */
        }
        values[i] = sum / config->average_count;
    }
    return 0;
}
