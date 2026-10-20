#include <gd32e50x.h>
#include <gd32_rcu.h>
#include <gd32e50x_fmc.h>
#include <gd32e50x_pmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define PLL_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(gd_gd32e505_pll)
#define HXTAL_HZ DT_PROP(DT_CLOCKS_CTLR(PLL_NODE), clock_frequency)
#define PLL_PREDIV DT_PROP(PLL_NODE, prediv)
#define PLL_MUL DT_PROP(PLL_NODE, mul)
#define CPU_HZ DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)
#define APB1_DIV DT_PROP(PLL_NODE, apb1_prescaler)
#define APB2_DIV DT_PROP(PLL_NODE, apb2_prescaler)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(gd_gd32e505_pll) == 1);
BUILD_ASSERT(HXTAL_HZ == HXTAL_VALUE, "HAL oscillator must match the PLL source");
BUILD_ASSERT(HXTAL_HZ >= 4000000 && HXTAL_HZ <= 32000000);
BUILD_ASSERT(HXTAL_HZ % PLL_PREDIV == 0);
BUILD_ASSERT(HXTAL_HZ / PLL_PREDIV >= 2000000 &&
             HXTAL_HZ / PLL_PREDIV <= 16000000, "PLL input must be 2..16 MHz");
BUILD_ASSERT(PLL_MUL >= 2 && PLL_MUL <= 63);
BUILD_ASSERT(HXTAL_HZ / PLL_PREDIV * PLL_MUL == CPU_HZ,
             "CPU frequency must match HXTAL / prediv * mul");
BUILD_ASSERT(CPU_HZ >= 16000000 && CPU_HZ <= 180000000);
BUILD_ASSERT(CPU_HZ / APB1_DIV <= 90000000, "APB1 exceeds 90 MHz");
BUILD_ASSERT(CPU_HZ / APB2_DIV <= 180000000, "APB2 exceeds 180 MHz");

static void wait_ready(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    for (uint32_t timeout = 0; timeout < 1000000U; ++timeout) {
        if ((*reg & mask) == expected) {
            return;
        }
    }
    /* Do not continue with an incorrect clock. Recovery is board-dependent. */
    while (true) {
    }
}

void soc_early_init_hook(void)
{
    /* The bootloader may have left PLL enabled: switch safely to IRC8M. */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    wait_ready(&RCU_CTL, RCU_CTL_IRC8MSTB, RCU_CTL_IRC8MSTB);
    rcu_system_clock_source_config(RCU_CKSYSSRC_IRC8M);
    wait_ready(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_IRC8M);
    rcu_deinit();

    /* Conservative wait states for the supported <=180 MHz range. */
    FMC_WS = (FMC_WS & ~FMC_WS_WSCNT) | WS_WSCNT(4);
    rcu_periph_clock_enable(RCU_PMU);
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb1_clock_config(UTIL_CAT(RCU_APB1_CKAHB_DIV, APB1_DIV));
    rcu_apb2_clock_config(UTIL_CAT(RCU_APB2_CKAHB_DIV, APB2_DIV));

    if (DT_PROP(PLL_NODE, hxtal_bypass)) {
        rcu_osci_bypass_mode_enable(RCU_HXTAL);
    }
    rcu_osci_on(RCU_HXTAL);
    wait_ready(&RCU_CTL, RCU_CTL_HXTALSTB, RCU_CTL_HXTALSTB);

    if (PLL_PREDIV == 2) {
        RCU_CFG0 |= RCU_CFG0_PREDV0;
    } else {
        RCU_CFG0 &= ~RCU_CFG0_PREDV0;
    }
    rcu_pll_config(RCU_PLLSRC_HXTAL_IRC48M, UTIL_CAT(RCU_PLL_MUL, PLL_MUL));
    rcu_osci_on(RCU_PLL_CK);
    wait_ready(&RCU_CTL, RCU_CTL_PLLSTB, RCU_CTL_PLLSTB);

    PMU_CTL0 |= PMU_CTL0_HDEN;
    wait_ready(&PMU_CS0, PMU_CS0_HDRF, PMU_CS0_HDRF);
    PMU_CTL0 |= PMU_CTL0_HDS;
    wait_ready(&PMU_CS0, PMU_CS0_HDSRF, PMU_CS0_HDSRF);

    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    wait_ready(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_PLL);
    SystemCoreClock = CPU_HZ;
}
