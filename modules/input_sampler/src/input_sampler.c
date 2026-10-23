#include <input_sampler/input_sampler.h>
#include <errno.h>

static int validate(const struct input_sampler_config *config)
{
    if (!config || (!config->analog && !config->digital_group_count) ||
        (config->digital_group_count && !config->digital_groups)) {
        return -EINVAL;
    }
    return 0;
}

int input_sampler_init(const struct input_sampler_config *config)
{
    int err = validate(config);

    if (err) { return err; }
    for (size_t i = 0; i < config->digital_group_count; ++i) {
        err = digital_io_init(&config->digital_groups[i]);
        if (err) { return err; }
    }
    return config->analog ? analog_input_init(config->analog) : 0;
}

int input_sampler_read(const struct input_sampler_config *config,
                       struct input_sampler_sample *sample)
{
    if (!sample) { return -EINVAL; }
    sample->valid = false;
    int err = validate(config);

    if (err) { return err; }
    if ((config->analog && !sample->analog) ||
        (config->digital_group_count && !sample->digital)) {
        return -EINVAL;
    }
    if ((config->analog && sample->analog_capacity < config->analog->channel_count) ||
        sample->digital_capacity < config->digital_group_count) {
        return -ENOMEM;
    }
    for (size_t i = 0; i < config->digital_group_count; ++i) {
        err = digital_io_read(&config->digital_groups[i], &sample->digital[i]);
        if (err) { return err; }
    }
    if (config->analog) {
        err = analog_input_read(config->analog, sample->analog, sample->analog_capacity);
    }
    sample->valid = !err;
    return err;
}
