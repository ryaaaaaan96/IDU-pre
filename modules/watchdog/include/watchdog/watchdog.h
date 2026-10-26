/**
 * @file
 * @brief 外部 GPIO 看门狗驱动接口。
 *
 * 各实例仅清零一次，实例和常量配置须比定时器存活更久。
 *
 * 初始化须串行， 错误查询和停止可与定时器并发。 喂狗 GPIO 必须支持 ISR 安全翻转， 应使用片上 GPIO 而非可休眠总线。
 * GPIO 失败及显式停止在设备复位前不可恢复；停止后引脚保持最后电平， 让外部看门狗到期。
 */
#ifndef WATCHDOG_GPIO_H
#define WATCHDOG_GPIO_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/**
 * @brief 外部看门狗的常驻引脚和周期配置。
 */
struct watchdog_config {
    struct gpio_dt_spec feed;
    uint32_t feed_interval_ms;
};

/**
 * @brief 调用方持有的看门狗实例，字段由驱动维护。
 */
struct watchdog {
    struct k_timer timer;
    struct k_spinlock lock;
    const struct watchdog_config *config;
    int error;
    bool initialized;
    bool active;
};

/**
 * @brief 配置喂狗 GPIO 并启动周期边沿输出。
 * @param[in,out] monitor 首次使用前清零的常驻看门狗实例。
 * @param[in] config 常驻引脚和非零喂狗周期配置。
 * @retval 0 初始化成功。
 * @retval -EALREADY 此实例已初始化。
 * @return 其他负错误码表示配置无效或硬件初始化失败。
 */
int watchdog_init(struct watchdog *monitor,
                  const struct watchdog_config *config);
/**
 * @brief 读取底层喂狗错误，可与定时器并发调用。
 * @param[in] monitor 待查询的看门狗实例。
 * @retval 0 当前无错误。
 * @retval -EINVAL 实例指针为空。
 * @retval -EACCES 实例尚未初始化。
 * @return 其他负错误码表示 GPIO 错误或已经停止。
 */
int watchdog_error(struct watchdog *monitor);
/**
 * @brief 停止周期喂狗，使外部看门狗到期。
 * @param[in,out] monitor 待停止的看门狗实例；NULL 时直接返回。
 * @note 可与定时器并发调用；停止后保持最后输出电平，复位前不恢复喂狗。
 */
void watchdog_stop(struct watchdog *monitor);

#endif
