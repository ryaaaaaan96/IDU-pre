#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

#include <zephyr/drivers/adc.h>

struct analog_input_config {
    const struct adc_dt_spec *channels;
    size_t channel_count;
    uint16_t average_count;
};

/* Thread APIs; configuration remains immutable while in use. Supports unsigned
 * single-ended samples up to 16 bits. Hardware setup is serialized by the caller.
 * Reads preserve configured channel order and round averages down. On failure,
 * preceding output elements may have changed; the entire sample is invalid.
 */
int analog_input_init(const struct analog_input_config *config);
int analog_input_read(const struct analog_input_config *config,
                      uint16_t *values, size_t capacity);

#endif
