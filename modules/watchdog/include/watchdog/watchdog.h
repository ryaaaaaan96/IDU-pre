#ifndef WATCHDOG_MONITOR_H
#define WATCHDOG_MONITOR_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

struct watchdog_config {
    struct gpio_dt_spec feed;
    uint32_t feed_interval_ms;
};

struct watchdog {
    struct k_timer timer;
    struct k_spinlock lock;
    const struct watchdog_config *config;
    int error;
    bool initialized;
    bool active;
};

/* 各实例仅清零一次，实例和常量配置须比定时器存活更久。初始化须串行，
 * 错误查询和停止可与定时器并发。
 * 喂狗 GPIO 必须支持 ISR 安全翻转，
 * 应使用片上 GPIO 而非可休眠总线。
 * GPIO 失败及显式停止在设备复位前不可恢复；停止后引脚保持最后电平，
 * 让外部看门狗到期。
 */
int watchdog_init(struct watchdog *monitor,
                  const struct watchdog_config *config);
int watchdog_error(struct watchdog *monitor);
void watchdog_stop(struct watchdog *monitor);

#endif
