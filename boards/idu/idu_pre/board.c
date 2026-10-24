#include <gd32_gpio.h>
#include <gd32_rcu.h>
#include <zephyr/platform/hooks.h>

void board_early_init_hook(void)
{
    /* PA15 驱动 RS485 DE：外设引脚初始化前释放 JTAG，保留 SWD。 */
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

#ifdef CONFIG_MCUBOOT
    /* PC13 浮空可在 MCUboot 擦除内部 Flash 前禁用外部看门狗。
     * 此钩子执行时 GPIO 驱动尚未初始化，因此直接使用 SoC HAL；
     * 应用在 status_init() 中重新启用看门狗。 */
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_13);
#endif
}
