#include "hardware/idu_pre.h"
#include <errno.h>
#include <gd32_gpio.h>
#include <gd32_rcu.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#define IO_NODE DT_NODELABEL(idu_io)
#define DI_SPEC(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec digital_inputs[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, di_gpios, DI_SPEC)
};
static const struct gpio_dt_spec address_switches[] = {
    DT_FOREACH_PROP_ELEM(IO_NODE, address_gpios, DI_SPEC)
};
static const struct gpio_dt_spec run_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

BUILD_ASSERT(ARRAY_SIZE(digital_inputs) == IDU_DI_COUNT);
BUILD_ASSERT(ARRAY_SIZE(address_switches) == 4);

static int configure_inputs(const struct gpio_dt_spec *pins, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!gpio_is_ready_dt(&pins[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&pins[i], GPIO_INPUT);
        if (err) {
            return err;
        }
    }
    return 0;
}

int idu_io_init(void)
{
    /* PA15 drives RS485 DE: release JTAG while retaining SWD. */
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

    int err = configure_inputs(digital_inputs, ARRAY_SIZE(digital_inputs));
    if (!err) {
        err = configure_inputs(address_switches, ARRAY_SIZE(address_switches));
    }
    if (err) {
        return err;
    }
    if (!gpio_is_ready_dt(&run_led)) {
        return -ENODEV;
    }
    return gpio_pin_configure_dt(&run_led, GPIO_OUTPUT_ACTIVE);
}

int idu_io_read(uint16_t *di, uint8_t *address)
{
    uint16_t bits = 0;
    uint8_t switches = 0;
    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs); ++i) {
        int state = gpio_pin_get_dt(&digital_inputs[i]);
        if (state < 0) {
            return state;
        }
        bits |= (uint16_t)(state << i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(address_switches); ++i) {
        int state = gpio_pin_get_dt(&address_switches[i]);
        if (state < 0) {
            return state;
        }
        switches = (switches << 1) | state;
    }
    *di = bits;
    *address = switches + 1;
    return 0;
}

int idu_led_toggle(void)
{
    return gpio_pin_toggle_dt(&run_led);
}
