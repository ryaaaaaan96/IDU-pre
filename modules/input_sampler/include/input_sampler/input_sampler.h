#ifndef INPUT_SAMPLER_H
#define INPUT_SAMPLER_H

#include <stdbool.h>
#include <analog_input/analog_input.h>
#include <gpio/digital_io.h>

struct input_sampler_config {
    const struct analog_input_config *analog;
    const struct digital_io_config *digital_groups;
    size_t digital_group_count;
};

struct input_sampler_sample {
    uint16_t *analog;
    size_t analog_capacity;
    uint32_t *digital;
    size_t digital_capacity;
    bool valid;
};

/* Thread APIs; the caller owns immutable configuration and output buffers and
 * serializes hardware access. Either analog or digital groups may be omitted.
 * Output order follows configuration. On any error valid is false; partial
 * output must not be consumed. Capacity errors occur before sampling starts.
 */
int input_sampler_init(const struct input_sampler_config *config);
int input_sampler_read(const struct input_sampler_config *config,
                       struct input_sampler_sample *sample);

#endif
