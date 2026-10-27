/** @file
 * @brief status 内部的指示灯接口，各状态使用独立的循环步骤。
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

/** @brief 指示状态独立于整机阶段，由 status 选择。 */
typedef enum {
    STATUS_LED_STARTING,
    STATUS_LED_RUN,
    STATUS_LED_FAULT,
    STATUS_LED_STATE_COUNT,
} status_led_state_t;

/** @brief 配置 GPIO 并显示初始状态；仅在启动线程调用一次。 */
int status_led_init(status_led_state_t state);
/** @brief 切换循环状态；同一状态不重启，可在线程或 ISR 中调用。 */
void status_led_set(status_led_state_t state);

#endif
