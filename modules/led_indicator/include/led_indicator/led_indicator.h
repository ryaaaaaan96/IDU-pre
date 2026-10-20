#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>

struct led_indicator {
    struct k_mutex lock;
    struct k_work_delayable work;
    struct led_dt_spec output;
    int64_t next_change;
    uint32_t on_ms, off_ms;
    int error;
    bool initialized, lit, blinking;
};

/* Zero-initialize once, serialize init, and keep the instance alive while work
 * can run. APIs are thread-only; runtime operations are mutex protected.
 * Uses standard LED drivers. Software blinking runs on the system workqueue,
 * so timing is best effort and the LED driver must not block that queue.
 * Set/toggle cancel blinking. Async errors stop blinking and remain observable
 * until the next explicit output command. State-to-pattern policy is external.
 */
int led_indicator_init(struct led_indicator *indicator, const struct led_dt_spec *output);
int led_indicator_set(struct led_indicator *indicator, bool on);
int led_indicator_toggle(struct led_indicator *indicator);
int led_indicator_blink(struct led_indicator *indicator, uint32_t on_ms, uint32_t off_ms);
int led_indicator_error(struct led_indicator *indicator);

#endif
