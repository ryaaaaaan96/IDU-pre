#ifndef TEST_PINCTRL_H
#define TEST_PINCTRL_H
#include <stdint.h>

struct pinctrl_dev_config {
    int unused;
};

#define PINCTRL_STATE_DEFAULT 0
extern int test_pin_error;

static inline int pinctrl_apply_state(const struct pinctrl_dev_config *pins, uint8_t state)
{
    (void)pins;
    (void)state;
    return test_pin_error;
}
#endif
