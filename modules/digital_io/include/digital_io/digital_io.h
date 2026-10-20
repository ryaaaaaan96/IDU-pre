#ifndef DIGITAL_IO_H
#define DIGITAL_IO_H

#include <zephyr/drivers/gpio.h>

enum digital_io_order { DIGITAL_IO_LSB_FIRST, DIGITAL_IO_MSB_FIRST };
struct digital_io_config {
    const struct gpio_dt_spec *pins;
    size_t count;
    enum digital_io_order order;
};

/* 1..32 inputs, logical active levels from gpio_dt_spec. No pin ownership or
 * device-specific address interpretation. Read leaves *bits unchanged on error.
 * GPIOs are read sequentially, not as an electrically simultaneous snapshot.
 */
int digital_io_init(const struct digital_io_config *config);
int digital_io_read(const struct digital_io_config *config, uint32_t *bits);

#endif
