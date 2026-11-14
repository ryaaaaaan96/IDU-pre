/* 独占定时器的输入捕获设备，实现 Zephyr 标准 PWM capture API。 */
#define DT_DRV_COMPAT gd_gd32_pwm_capture

#include <errno.h>
#include <gd32_timer.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
#include <gd32_gpio.h>
#include <gd32_rcu.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gd32_capture, LOG_LEVEL_INF);
#endif

struct capture_config {
    uint32_t reg;
    uint16_t clock_id;
    uint16_t prescaler;
    uint8_t capture_shift;
    uint8_t channel;
    bool is_32bit;
    uint16_t up_irq;
    uint16_t cc_irq;
    struct reset_dt_spec reset;
    const struct pinctrl_dev_config *pins;
    void (*connect_irq)(void);
};

struct capture_data {
    struct k_spinlock lock;
    uint64_t wraps;
    uint64_t previous;
    uint32_t rate;
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
    uint32_t update_irqs;
    uint32_t capture_irqs;
#endif
    bool have_previous;
    bool enabled;
    pwm_flags_t flags;
    pwm_capture_callback_handler_t callback;
    void *context;
};

static void capture_isr(const void *arg)
{
    const struct device *dev = arg;
    const struct capture_config *c = dev->config;
    struct capture_data *d = dev->data;
    k_spinlock_key_t key = k_spin_lock(&d->lock);
    uint32_t flags = TIMER_INTF(c->reg);
    uint32_t captured = 0;
    if (flags & BIT(c->channel + 1U)) {
        captured = REG32(c->reg + 0x34U + 4U * c->channel);
    }
    /* 输入模式读 CV 清捕获标志；处理读取过程中发生的回绕。 */
    flags |= TIMER_INTF(c->reg) & (TIMER_INTF_UPIF | BIT(c->channel + 9U));
    uint64_t base = d->wraps;
    if (flags & TIMER_INTF_UPIF) {
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
        d->update_irqs++;
#endif
        TIMER_INTF(c->reg) = ~TIMER_INTF_UPIF;
        d->wraps += c->is_32bit ? (UINT64_C(1) << 32) : UINT64_C(65536);
        if (captured < (c->is_32bit ? UINT32_C(0x80000000) : UINT32_C(32768))) {
            base = d->wraps;
        }
    }
    bool notify = false;
    int status = 0;
    uint32_t period = 0;
    if (d->enabled && (flags & BIT(c->channel + 9U))) {
        TIMER_INTF(c->reg) = ~BIT(c->channel + 9U);
        d->have_previous = false;
        notify = true;
        status = -EOVERFLOW;
    } else if (d->enabled && (flags & BIT(c->channel + 1U))) {
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
        d->capture_irqs++;
#endif
        uint64_t stamp = base + captured;
        if (d->have_previous) {
            uint64_t delta = stamp - d->previous;
            /* 硬件每 N 个边沿捕获一次，以移位归一化为标准单周期计数。 */
            delta >>= c->capture_shift;
            status = delta > UINT32_MAX || !delta ? -ERANGE : 0;
            period = status ? 0 : (uint32_t)delta;
            notify = true;
        }
        d->previous = stamp;
        d->have_previous = true;
    }
    if (notify && !(d->flags & PWM_CAPTURE_MODE_CONTINUOUS)) {
        TIMER_DMAINTEN(c->reg) = 0;
        TIMER_CTL0(c->reg) = 0;
        d->enabled = false;
    }
    pwm_capture_callback_handler_t callback = d->callback;
    void *context = d->context;
    k_spin_unlock(&d->lock, key);
    /* 不持驱动状态锁调用上层；回调可以停止当前捕获。 */
    if (notify && callback) {
        callback(dev, c->channel, period, 0, status, context);
    }
}

static int configure(const struct device *dev,
    uint32_t channel,
    pwm_flags_t flags,
    pwm_capture_callback_handler_t callback,
    void *context)
{
    struct capture_data *d = dev->data;
    const struct capture_config *c = dev->config;
    if (channel != c->channel || (flags & PWM_CAPTURE_TYPE_MASK) != PWM_CAPTURE_TYPE_PERIOD ||
        (flags & ~(PWM_CAPTURE_TYPE_MASK | PWM_CAPTURE_MODE_MASK | PWM_POLARITY_INVERTED))) {
        return -ENOTSUP;
    }
    k_spinlock_key_t key = k_spin_lock(&d->lock);
    if (d->enabled) {
        k_spin_unlock(&d->lock, key);
        return -EBUSY;
    }
    d->flags = flags;
    d->callback = callback;
    d->context = context;
    k_spin_unlock(&d->lock, key);
    return 0;
}

static int enable(const struct device *dev, uint32_t channel)
{
    const struct capture_config *c = dev->config;
    struct capture_data *d = dev->data;
    if (channel != c->channel) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&d->lock);
    int err = d->enabled ? -EBUSY : !d->callback ? -EINVAL : 0;
    if (!err) {
        d->wraps = 0;
        d->have_previous = false;
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
        d->update_irqs = 0;
        d->capture_irqs = 0;
#endif
        TIMER_CTL0(c->reg) = 0;
        TIMER_CNT(c->reg) = 0;
        TIMER_CHCTL0(c->reg) = 0;
        TIMER_CHCTL1(c->reg) = 0;
        /* CH0/1 位于 CHCTL0，CH2/3 位于 CHCTL1，每通道占八位。 */
        REG32(c->reg + 0x18U + (c->channel / 2U) * 4U) =
            (TIMER_IC_SELECTION_DIRECTTI | (c->capture_shift << 2U)) << ((c->channel % 2U) * 8U);
        TIMER_CHCTL2(c->reg) = BIT(4U * c->channel) |
                               ((d->flags & PWM_POLARITY_INVERTED) ? BIT(4U * c->channel + 1U) : 0);
        TIMER_SWEVG(c->reg) = TIMER_SWEVG_UPG;
        TIMER_INTF(c->reg) = 0;
        d->enabled = true;
        TIMER_DMAINTEN(c->reg) = TIMER_DMAINTEN_UPIE | BIT(c->channel + 1U);
        TIMER_CTL0(c->reg) = TIMER_CTL0_CEN;
    }
    k_spin_unlock(&d->lock, key);
    return err;
}

static int disable(const struct device *dev, uint32_t channel)
{
    const struct capture_config *c = dev->config;
    struct capture_data *d = dev->data;
    if (channel != c->channel) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&d->lock);
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
    bool diagnose = d->enabled && !k_is_in_isr();
    uint32_t count = TIMER_CNT(c->reg);
    uint32_t control = TIMER_CTL0(c->reg);
    uint32_t flags = TIMER_INTF(c->reg);
    uint32_t interrupts = TIMER_DMAINTEN(c->reg);
    uint32_t chctl0 = TIMER_CHCTL0(c->reg);
    uint32_t chctl2 = TIMER_CHCTL2(c->reg);
    uint32_t updates = d->update_irqs;
    uint32_t captures = d->capture_irqs;
#endif
    TIMER_DMAINTEN(c->reg) = 0;
    TIMER_CTL0(c->reg) = 0;
    TIMER_CHCTL2(c->reg) = 0;
    TIMER_INTF(c->reg) = 0;
    d->enabled = false;
    k_spin_unlock(&d->lock, key);
#ifdef CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS
    if (diagnose) {
        LOG_INF("Capture diagnostic: base=%08x rate=%u psc=%u car=%u "
                "cnt=%u ctl0=%08x intf=%08x inten=%08x chctl0=%08x chctl2=%08x "
                "up_irq=%u cc_irq=%u up_enabled=%d cc_enabled=%d updates=%u captures=%u",
            c->reg,
            d->rate,
            TIMER_PSC(c->reg),
            TIMER_CAR(c->reg),
            count,
            control,
            flags,
            interrupts,
            chctl0,
            chctl2,
            c->up_irq,
            c->cc_irq,
            !!irq_is_enabled(c->up_irq),
            !!irq_is_enabled(c->cc_irq),
            updates,
            captures);
        LOG_INF("Capture clocks: apb2en=%08x apb2rst=%08x afio_pcf0=%08x",
            RCU_APB2EN,
            RCU_APB2RST,
            AFIO_PCF0);
        const struct pinctrl_state *state;
        if (!pinctrl_lookup_state(c->pins, PINCTRL_STATE_DEFAULT, &state) && state->pin_cnt) {
            pinctrl_soc_pin_t pin = state->pins[0];
            uint32_t port = GPIOA + GD32_PORT_GET(pin) * 0x400U;
            uint32_t number = GD32_PIN_GET(pin);
            uint32_t ctl = number < 8 ? GPIO_CTL0(port) : GPIO_CTL1(port);
            unsigned previous = !!(GPIO_ISTAT(port) & BIT(number));
            unsigned transitions = 0;
            /* 被动观察引脚，不改变配置；只在线程超时诊断中短时采样。 */
            for (unsigned i = 0; i < 16; ++i) {
                k_busy_wait(100);
                unsigned level = !!(GPIO_ISTAT(port) & BIT(number));
                transitions += level != previous;
                previous = level;
            }
            LOG_INF("Capture pin: P%c%u mode=%x level=%u transitions_1600us=%u",
                'A' + GD32_PORT_GET(pin),
                number,
                (ctl >> ((number % 8U) * 4U)) & 0xfU,
                previous,
                transitions);
        }
    }
#endif
    return 0;
}

static int get_rate(const struct device *dev, uint32_t channel, uint64_t *rate)
{
    struct capture_data *d = dev->data;
    const struct capture_config *c = dev->config;
    if (channel != c->channel) {
        return -EINVAL;
    }
    *rate = d->rate;
    return 0;
}

static int set_cycles(
    const struct device *dev, uint32_t channel, uint32_t period, uint32_t pulse, pwm_flags_t flags)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(channel);
    ARG_UNUSED(period);
    ARG_UNUSED(pulse);
    ARG_UNUSED(flags);
    return -ENOTSUP;
}

static DEVICE_API(pwm, api) = {
    .set_cycles = set_cycles,
    .get_cycles_per_sec = get_rate,
    .configure_capture = configure,
    .enable_capture = enable,
    .disable_capture = disable,
};

static int init(const struct device *dev)
{
    const struct capture_config *c = dev->config;
    struct capture_data *d = dev->data;
    uint32_t rate;
    int err = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&c->clock_id);
    if (err) {
        return err;
    }
    err =
        clock_control_get_rate(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&c->clock_id, &rate);
    if (err) {
        return err;
    }
    err = reset_line_toggle_dt(&c->reset);
    if (err) {
        return err;
    }
    err = pinctrl_apply_state(c->pins, PINCTRL_STATE_DEFAULT);
    if (err) {
        return err;
    }
    d->rate = rate / (c->prescaler + 1U);
    TIMER_CTL0(c->reg) = 0;
    TIMER_DMAINTEN(c->reg) = 0;
    TIMER_PSC(c->reg) = c->prescaler;
    TIMER_CAR(c->reg) = c->is_32bit ? UINT32_MAX : UINT16_MAX;
    c->connect_irq();
    return d->rate ? 0 : -EINVAL;
}

/* 通用定时器使用单个 global IRQ，高级定时器沿用分立 UP/CC IRQ。 */
#define PARENT(i) DT_INST_PARENT(i)
#define UP_NAME(i) COND_CODE_1(DT_IRQ_HAS_NAME(PARENT(i), global), (global), (up))
#define CC_NAME(i) COND_CODE_1(DT_IRQ_HAS_NAME(PARENT(i), global), (global), (cc))
#define CONNECT_GLOBAL(i)                                                                          \
    IRQ_CONNECT(DT_IRQ_BY_NAME(PARENT(i), global, irq),                                            \
        DT_IRQ_BY_NAME(PARENT(i), global, priority),                                               \
        capture_isr,                                                                               \
        DEVICE_DT_INST_GET(i),                                                                     \
        0);                                                                                        \
    irq_enable(DT_IRQ_BY_NAME(PARENT(i), global, irq));
#define CONNECT_SPLIT(i)                                                                           \
    IRQ_CONNECT(DT_IRQ_BY_NAME(PARENT(i), up, irq),                                                \
        DT_IRQ_BY_NAME(PARENT(i), up, priority),                                                   \
        capture_isr,                                                                               \
        DEVICE_DT_INST_GET(i),                                                                     \
        0);                                                                                        \
    IRQ_CONNECT(DT_IRQ_BY_NAME(PARENT(i), cc, irq),                                                \
        DT_IRQ_BY_NAME(PARENT(i), cc, priority),                                                   \
        capture_isr,                                                                               \
        DEVICE_DT_INST_GET(i),                                                                     \
        0);                                                                                        \
    irq_enable(DT_IRQ_BY_NAME(PARENT(i), up, irq));                                                \
    irq_enable(DT_IRQ_BY_NAME(PARENT(i), cc, irq));
#define DEFINE_CAPTURE(i)                                                                          \
    BUILD_ASSERT(!IS_ENABLED(CONFIG_COUNTER_TIMER_GD32), "Capture conflicts with counter");        \
    BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_CHILD(PARENT(i), pwm), okay),                              \
        "Capture conflicts with timer PWM output");                                                \
    BUILD_ASSERT(DT_INST_PROP(i, capture_channel) < DT_PROP(PARENT(i), channels),                  \
        "Capture channel exceeds timer channel count");                                            \
    PINCTRL_DT_INST_DEFINE(i);                                                                     \
    static struct capture_data data_##i;                                                           \
    static void connect_##i(void)                                                                  \
    {                                                                                              \
        COND_CODE_1(DT_IRQ_HAS_NAME(PARENT(i), global), (CONNECT_GLOBAL(i)), (CONNECT_SPLIT(i)))   \
    }                                                                                              \
    static const struct capture_config config_##i = {                                              \
        .reg = DT_REG_ADDR(PARENT(i)),                                                             \
        .clock_id = DT_CLOCKS_CELL(PARENT(i), id),                                                 \
        .prescaler = DT_PROP(PARENT(i), prescaler),                                                \
        .capture_shift = LOG2(DT_INST_PROP(i, capture_prescaler)),                                 \
        .channel = DT_INST_PROP(i, capture_channel),                                               \
        .is_32bit = DT_PROP(PARENT(i), is_32bit),                                                  \
        .up_irq = DT_IRQ_BY_NAME(PARENT(i), UP_NAME(i), irq),                                      \
        .cc_irq = DT_IRQ_BY_NAME(PARENT(i), CC_NAME(i), irq),                                      \
        .reset = RESET_DT_SPEC_GET(PARENT(i)),                                                     \
        .pins = PINCTRL_DT_INST_DEV_CONFIG_GET(i),                                                 \
        .connect_irq = connect_##i,                                                                \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(                                                                         \
        i, init, NULL, &data_##i, &config_##i, POST_KERNEL, CONFIG_PWM_INIT_PRIORITY, &api);
DT_INST_FOREACH_STATUS_OKAY(DEFINE_CAPTURE)
