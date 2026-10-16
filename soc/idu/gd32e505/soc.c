#include <gd32e50x.h>
#include <gd32_rcu.h>
#include <gd32e50x_fmc.h>
#include <gd32e50x_pmu.h>
#include <gd32_gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(DT_PROP(DT_NODELABEL(hxtal), clock_frequency) == 20000000,
             "PLL setup requires the confirmed 20 MHz active clock");
BUILD_ASSERT(DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency) == 180000000);

static void wait_ready(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    for (uint32_t timeout = 0; timeout < 1000000U; ++timeout) {
        if ((*reg & mask) == expected) {
            return;
        }
    }
    /* Do not continue with an incorrect clock. The external watchdog resets us. */
    while (true) {
    }
}

void soc_early_init_hook(void)
{
    /* The legacy bootloader may have left PLL enabled: switch safely to IRC8M. */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    wait_ready(&RCU_CTL, RCU_CTL_IRC8MSTB, RCU_CTL_IRC8MSTB);
    rcu_system_clock_source_config(RCU_CKSYSSRC_IRC8M);
    wait_ready(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_IRC8M);
    rcu_deinit();

#ifdef CONFIG_MCUBOOT
    /* Match the proven legacy BOOT's wdog_close(): PC13 floating disables
     * this board's external watchdog during internal-flash erase. A timer
     * in internal flash cannot guarantee feeding while that flash is busy.
     * The application enables it again in idu_health_init(). */
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_13);
#endif

    FMC_WS = (FMC_WS & ~FMC_WS_WSCNT) | WS_WSCNT(4);
    rcu_periph_clock_enable(RCU_PMU);
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);

    rcu_osci_bypass_mode_enable(RCU_HXTAL);
    rcu_osci_on(RCU_HXTAL);
    wait_ready(&RCU_CTL, RCU_CTL_HXTALSTB, RCU_CTL_HXTALSTB);

    /* E505 high-density-plus HAL: HXTAL / 2 * 18 = 180 MHz. */
    RCU_CFG0 |= RCU_CFG0_PREDV0;
    rcu_pll_config(RCU_PLLSRC_HXTAL_IRC48M, RCU_PLL_MUL18);
    rcu_osci_on(RCU_PLL_CK);
    wait_ready(&RCU_CTL, RCU_CTL_PLLSTB, RCU_CTL_PLLSTB);

    PMU_CTL0 |= PMU_CTL0_HDEN;
    wait_ready(&PMU_CS0, PMU_CS0_HDRF, PMU_CS0_HDRF);
    PMU_CTL0 |= PMU_CTL0_HDS;
    wait_ready(&PMU_CS0, PMU_CS0_HDSRF, PMU_CS0_HDSRF);

    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    wait_ready(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_PLL);
    SystemCoreClock = 180000000U;
}
