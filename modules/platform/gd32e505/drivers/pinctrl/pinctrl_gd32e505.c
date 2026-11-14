/*
 * Copyright (c) 2021 Teslabs Engineering S.L.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>

#include <gd32_gpio.h>

/** AFIO DT node */
#define AFIO_NODE DT_NODELABEL(afio)

/** GPIO mode: input floating (CTL bits) */
#define GPIO_MODE_INP_FLOAT 0x4U
/** GPIO mode: input with pull-up/down (CTL bits) */
#define GPIO_MODE_INP_PUPD 0x8U
/** GPIO mode: output push-pull (CTL bits) */
#define GPIO_MODE_ALT_PP 0x8U
/** GPIO mode: output open-drain (CTL bits) */
#define GPIO_MODE_ALT_OD 0xCU

/** Utility macro that expands to the GPIO port address if it exists */
#define GD32_PORT_ADDR_OR_NONE(nodelabel)                                                          \
    COND_CODE_1(                                                                                   \
        DT_NODE_EXISTS(DT_NODELABEL(nodelabel)), (DT_REG_ADDR(DT_NODELABEL(nodelabel)), ), ())

/** Utility macro that expands to the GPIO clock id if it exists */
#define GD32_PORT_CLOCK_ID_OR_NONE(nodelabel)                                                      \
    COND_CODE_1(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)),                                           \
        (DT_CLOCKS_CELL(DT_NODELABEL(nodelabel), id), ),                                           \
        ())

/** GD32 port addresses */
static const uint32_t gd32_port_addrs[] = {GD32_PORT_ADDR_OR_NONE(gpioa) GD32_PORT_ADDR_OR_NONE(
    gpiob) GD32_PORT_ADDR_OR_NONE(gpioc) GD32_PORT_ADDR_OR_NONE(gpiod) GD32_PORT_ADDR_OR_NONE(gpioe)
        GD32_PORT_ADDR_OR_NONE(gpiof) GD32_PORT_ADDR_OR_NONE(gpiog)};

/** GD32 port clock identifiers */
static const uint16_t gd32_port_clkids[] = {GD32_PORT_CLOCK_ID_OR_NONE(gpioa)
        GD32_PORT_CLOCK_ID_OR_NONE(gpiob) GD32_PORT_CLOCK_ID_OR_NONE(gpioc)
            GD32_PORT_CLOCK_ID_OR_NONE(gpiod) GD32_PORT_CLOCK_ID_OR_NONE(gpioe)
                GD32_PORT_CLOCK_ID_OR_NONE(gpiof) GD32_PORT_CLOCK_ID_OR_NONE(gpiog)};

/**
 * @brief Initialize AFIO
 *
 * This function enables AFIO clock and configures the I/O compensation if
 * available and enabled in Devicetree.
 *
 * @retval 0 Always
 */
static int afio_init(void)
{
    uint16_t clkid = DT_CLOCKS_CELL(AFIO_NODE, id);

    (void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&clkid);

#ifdef AFIO_CPSCTL
    if (DT_PROP(AFIO_NODE, enable_cps)) {
        gpio_compensation_config(GPIO_COMPENSATION_ENABLE);
        while (gpio_compensation_flag_get() == RESET) {
        }
    }
#endif /* AFIO_CPSCTL */

    return 0;
}

SYS_INIT(afio_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/**
 * @brief Helper function to configure the SPD register if available.
 *
 * @param port GPIO port address.
 * @param pin_bit GPIO pin, set as bit position.
 * @param speed GPIO speed.
 *
 * @return Value of the mode register (speed) that should be set later.
 */
static inline uint8_t configure_spd(uint32_t port, uint32_t pin_bit, uint8_t speed)
{
    switch (speed) {
    case GD32_OSPEED_MAX:
#ifdef GPIOx_SPD
        GPIOx_SPD(port) |= pin_bit;
#endif /* GPIOx_SPD */
        return speed;
    default:
#ifdef GPIOx_SPD
        GPIOx_SPD(port) &= ~pin_bit;
#endif /* GPIOx_SPD */
        return speed + 1U;
    }
}

/**
 * @brief Configure a pin.
 *
 * @param pin The pin to configure.
 */
static void configure_pin(pinctrl_soc_pin_t pin)
{
    uint8_t port_idx, mode, pin_num;
    uint32_t port, pin_bit, reg_val;
    volatile uint32_t *reg;
    uint16_t clkid;

    port_idx = GD32_PORT_GET(pin);
    __ASSERT_NO_MSG(port_idx < ARRAY_SIZE(gd32_port_addrs));

    clkid = gd32_port_clkids[port_idx];
    port = gd32_port_addrs[port_idx];
    pin_num = GD32_PIN_GET(pin);
    pin_bit = BIT(pin_num);
    mode = GD32_MODE_GET(pin);

    if (pin_num < 8U) {
        reg = &GPIO_CTL0(port);
    } else {
        reg = &GPIO_CTL1(port);
        pin_num -= 8U;
    }

    (void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&clkid);

    reg_val = *reg;
    reg_val &= ~GPIO_MODE_MASK(pin_num);

    if (mode == GD32_MODE_ALTERNATE) {
        uint8_t new_mode;

        new_mode = configure_spd(port, pin_bit, GD32_OSPEED_GET(pin));

        if (GD32_OTYPE_GET(pin) == GD32_OTYPE_PP) {
            new_mode |= GPIO_MODE_ALT_PP;
        } else {
            new_mode |= GPIO_MODE_ALT_OD;
        }

        reg_val |= GPIO_MODE_SET(pin_num, new_mode);
    } else if (mode == GD32_MODE_GPIO_IN) {
        uint8_t pupd = GD32_PUPD_GET(pin);

        if (pupd == GD32_PUPD_NONE) {
            reg_val |= GPIO_MODE_SET(pin_num, GPIO_MODE_INP_FLOAT);
        } else {
            reg_val |= GPIO_MODE_SET(pin_num, GPIO_MODE_INP_PUPD);

            if (pupd == GD32_PUPD_PULLDOWN) {
                GPIO_BC(port) = pin_bit;
            } else if (pupd == GD32_PUPD_PULLUP) {
                GPIO_BOP(port) = pin_bit;
            }
        }
    }

    *reg = reg_val;
}

/**
 * @brief Configure remap.
 *
 * @param remap Remap bit field as encoded by #GD32_REMAP.
 */
static void configure_remap(uint16_t remap)
{
    uint8_t pos;
    uint32_t reg_val;
    volatile uint32_t *reg;

    /* not remappable */
    if (remap == GD32_NORMP) {
        return;
    }

    if (GD32_REMAP_REG_GET(remap) == 0U) {
        reg = &AFIO_PCF0;
    } else {
        reg = &AFIO_PCF1;
    }

    pos = GD32_REMAP_POS_GET(remap);

    reg_val = *reg;
    reg_val &= ~(GD32_REMAP_MSK_GET(remap) << pos);
    reg_val |= GD32_REMAP_VAL_GET(remap) << pos;
    *reg = reg_val;
}

static const uint32_t afcfg_masks[7][16] = {
    ['A' - 'A'][2] = AFIO_PCFA_PA2_AFCFG,
    ['A' - 'A'][3] = AFIO_PCFA_PA3_AFCFG,
    ['A' - 'A'][5] = AFIO_PCFA_PA5_AFCFG,
    ['A' - 'A'][8] = AFIO_PCFA_PA8_AFCFG,
    ['A' - 'A'][9] = AFIO_PCFA_PA9_AFCFG,
    ['A' - 'A'][10] = AFIO_PCFA_PA10_AFCFG,
    ['A' - 'A'][11] = AFIO_PCFA_PA11_AFCFG,
    ['A' - 'A'][12] = AFIO_PCFA_PA12_AFCFG,
    ['A' - 'A'][15] = AFIO_PCFA_PA15_AFCFG,
    ['B' - 'A'][0] = AFIO_PCFB_PB0_AFCFG,
    ['B' - 'A'][1] = AFIO_PCFB_PB1_AFCFG,
    ['B' - 'A'][2] = AFIO_PCFB_PB2_AFCFG,
    ['B' - 'A'][3] = AFIO_PCFB_PB3_AFCFG,
    ['B' - 'A'][4] = AFIO_PCFB_PB4_AFCFG,
    ['B' - 'A'][5] = AFIO_PCFB_PB5_AFCFG,
    ['B' - 'A'][6] = AFIO_PCFB_PB6_AFCFG,
    ['B' - 'A'][7] = AFIO_PCFB_PB7_AFCFG,
    ['B' - 'A'][8] = AFIO_PCFB_PB8_AFCFG,
    ['B' - 'A'][9] = AFIO_PCFB_PB9_AFCFG,
    ['B' - 'A'][10] = AFIO_PCFB_PB10_AFCFG,
    ['B' - 'A'][11] = AFIO_PCFB_PB11_AFCFG,
    ['B' - 'A'][12] = AFIO_PCFB_PB12_AFCFG,
    ['B' - 'A'][13] = AFIO_PCFB_PB13_AFCFG,
    ['B' - 'A'][14] = AFIO_PCFB_PB14_AFCFG,
    ['B' - 'A'][15] = AFIO_PCFB_PB15_AFCFG,
    ['C' - 'A'][0] = AFIO_PCFC_PC0_AFCFG,
    ['C' - 'A'][2] = AFIO_PCFC_PC2_AFCFG,
    ['C' - 'A'][3] = AFIO_PCFC_PC3_AFCFG,
    ['C' - 'A'][6] = AFIO_PCFC_PC6_AFCFG,
    ['C' - 'A'][7] = AFIO_PCFC_PC7_AFCFG,
    ['C' - 'A'][8] = AFIO_PCFC_PC8_AFCFG,
    ['C' - 'A'][9] = AFIO_PCFC_PC9_AFCFG,
    ['C' - 'A'][10] = AFIO_PCFC_PC10_AFCFG,
    ['C' - 'A'][11] = AFIO_PCFC_PC11_AFCFG,
    ['C' - 'A'][12] = AFIO_PCFC_PC12_AFCFG,
    ['D' - 'A'][4] = AFIO_PCFD_PD4_AFCFG,
    ['D' - 'A'][5] = AFIO_PCFD_PD5_AFCFG,
    ['E' - 'A'][0] = AFIO_PCFE_PE0_AFCFG,
    ['E' - 'A'][1] = AFIO_PCFE_PE1_AFCFG,
    ['E' - 'A'][8] = AFIO_PCFE_PE8_AFCFG,
    ['E' - 'A'][9] = AFIO_PCFE_PE9_AFCFG,
    ['E' - 'A'][10] = AFIO_PCFE_PE10_AFCFG,
    ['E' - 'A'][11] = AFIO_PCFE_PE11_AFCFG,
    ['E' - 'A'][12] = AFIO_PCFE_PE12_AFCFG,
    ['E' - 'A'][13] = AFIO_PCFE_PE13_AFCFG,
    ['G' - 'A'][6] = AFIO_PCFG_PG6_AFCFG,
    ['G' - 'A'][7] = AFIO_PCFG_PG7_AFCFG,
    ['G' - 'A'][9] = AFIO_PCFG_PG9_AFCFG,
    ['G' - 'A'][10] = AFIO_PCFG_PG10_AFCFG,
    ['G' - 'A'][11] = AFIO_PCFG_PG11_AFCFG,
    ['G' - 'A'][12] = AFIO_PCFG_PG12_AFCFG,
    ['G' - 'A'][13] = AFIO_PCFG_PG13_AFCFG,
    ['G' - 'A'][14] = AFIO_PCFG_PG14_AFCFG,
};

/* 选择值由官方 HAL 的 AFIO_PCFx 定义生成，零值恢复 Alternate2。 */
static void configure_afcfg(pinctrl_soc_pin_t pin)
{
    uint8_t port = GD32_PORT_GET(pin);
    uint8_t number = GD32_PIN_GET(pin);
    uint32_t mask = afcfg_masks[port][number];
    uint32_t value = (pin >> 20U) & 3U;

    if (mask != 0U) {
        volatile uint32_t *reg = (volatile uint32_t *)(AFIO + 0x3CU + port * 4U);
        *reg = (*reg & ~mask) | ((value << (number * 2U)) & mask);
    }
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
    ARG_UNUSED(reg);

    if (pin_cnt == 0U) {
        return -EINVAL;
    }

    /* 各引脚可有独立选择，不能只处理首个引脚。 */

    /* configure all pins */
    for (uint8_t i = 0U; i < pin_cnt; i++) {
        configure_remap(GD32_REMAP_GET(pins[i]));
        configure_afcfg(pins[i]);
        configure_pin(pins[i]);
    }

    return 0;
}
