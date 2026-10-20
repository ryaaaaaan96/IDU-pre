#include <gd32_gpio.h>
#include <gd32_rcu.h>
#include <zephyr/platform/hooks.h>

void board_early_init_hook(void)
{
    /* Match legacy BOOT's wdog_close(): PC13 floating disables the external
     * watchdog before MCUboot can erase internal flash. GPIO drivers are not
     * initialized at this hook, so use the SoC HAL directly.
     * The application enables the watchdog again in idu_health_init(). */
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_13);
}
