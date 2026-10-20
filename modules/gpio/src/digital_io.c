#include <gpio/digital_io.h>
#include <errno.h>

static int validate(const struct digital_io_config *config)
{
    if (!config || !config->pins || !config->count || config->count > 32 ||
        (config->order != DIGITAL_IO_LSB_FIRST && config->order != DIGITAL_IO_MSB_FIRST)) {
        return -EINVAL;
    }
    for (size_t i = 0; i < config->count; ++i) {
        if (!config->pins[i].port || !gpio_is_ready_dt(&config->pins[i])) {
            return -ENODEV;
        }
        const struct gpio_driver_config *port = config->pins[i].port->config;
        if (!port || config->pins[i].pin >= GPIO_MAX_PINS_PER_PORT ||
            !(port->port_pin_mask & BIT(config->pins[i].pin))) { return -EINVAL; }
    }
    return 0;
}

int digital_io_init(const struct digital_io_config *config)
{
    int err = validate(config);
    if (err) { return err; }
    for (size_t i = 0; i < config->count; ++i) {
        err = gpio_pin_configure_dt(&config->pins[i], GPIO_INPUT);
        if (err) { return err; }
    }
    return 0;
}

int digital_io_read(const struct digital_io_config *config, uint32_t *bits)
{
    if (!bits) { return -EINVAL; }
    int err = validate(config);
    if (err) { return err; }
    uint32_t value = 0;
    for (size_t i = 0; i < config->count; ++i) {
        int state = gpio_pin_get_dt(&config->pins[i]);
        if (state < 0) { return state; }
        size_t bit = config->order == DIGITAL_IO_LSB_FIRST ? i : config->count - 1 - i;
        value |= (uint32_t)state << bit;
    }
    *bits = value;
    return 0;
}
