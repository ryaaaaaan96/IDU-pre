#include "acquisition_internal.h"
#include "protocol/inc/process_table.h"
#include "protocol/inc/signal_keys.h"
#include <dataprocess/dataprocess.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif
#include <errno.h>

#define DI_GPIO_SPEC(alias) GPIO_DT_SPEC_GET(DT_ALIAS(alias), gpios)

#define DI_SAMPLE_PERIOD_MS 500U

/* 按物理 DI 编号排序，全部输入共用一个采样周期。 */
static const struct gpio_dt_spec digital_inputs[] = {
    DI_GPIO_SPEC(di1),
    DI_GPIO_SPEC(di2),
    DI_GPIO_SPEC(di3),
    DI_GPIO_SPEC(di4),
    DI_GPIO_SPEC(di5),
    DI_GPIO_SPEC(di6),
    DI_GPIO_SPEC(di7),
    DI_GPIO_SPEC(di8),
    DI_GPIO_SPEC(di9),
    DI_GPIO_SPEC(di10),
};

static const process_signal_index_t digital_points[] = {PROCESS_DI};
static int64_t next_sample_ms;
BUILD_ASSERT(ARRAY_SIZE(digital_inputs) == 10);

static atomic_t inputs_ready;

int di_init(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs); ++i) {
        if (!gpio_is_ready_dt(&digital_inputs[i])) {
            return -ENODEV;
        }
        int err = gpio_pin_configure_dt(&digital_inputs[i], GPIO_INPUT);
        if (err) {
            return err;
        }
    }

    atomic_set(&inputs_ready, 1);
    return 0;
}

int di_acquire(int64_t *next_deadline_ms)
{
    int64_t now = k_uptime_get();
    if (now < next_sample_ms) {
        *next_deadline_ms = next_sample_ms;
        return 0;
    }

    int err;
    uint16_t digital_values[ARRAY_SIZE(digital_points)] = {0};
    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs); ++i) {
        const struct gpio_dt_spec *spec = &digital_inputs[i];
        int value = gpio_pin_get_dt(spec);
        if (value < 0) {
            return value;
        }
        /* 有效电平表示正常；协议位图中异常为 1。 */
        if (!value) {
            digital_values[i / 16] |= BIT(i % 16);
        }
    }
    for (size_t group = 0; group < ARRAY_SIZE(digital_points); ++group) {
        value_t value = {.u16 = digital_values[group]};
        err = dp_set_value_by_index(&(dp_index_access_t){.device_id = DEVICE_LOCAL,
                                        .signal_index = digital_points[group],
                                        .value_type = VALUE_TYPE_U16,
                                        .flags = DP_ACCESS_FLAG_NONE},
            &value);
        if (err) {
            return err;
        }
    }
    /* 首次采样建立时间轴；超期跳过，不连续补采。 */
    int64_t completed_ms = k_uptime_get();
    int64_t deadline = next_sample_ms ? next_sample_ms : now;
    next_sample_ms = acquisition_next_deadline(deadline, DI_SAMPLE_PERIOD_MS, completed_ms);
    *next_deadline_ms = next_sample_ms;
    return 0;
}

#if defined(CONFIG_SHELL)
/* 直接读取 GPIO，不更新 DP，也不触发测点回调。 */
static int cmd_inputs_show(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!atomic_get(&inputs_ready)) {
        shell_error(shell, "Inputs are not initialized");
        return -EAGAIN;
    }

    for (size_t i = 0; i < ARRAY_SIZE(digital_inputs); ++i) {
        const struct gpio_dt_spec *spec = &digital_inputs[i];
        int level = gpio_pin_get_raw(spec->port, spec->pin);
        if (level < 0) {
            shell_error(shell, "DI%u read failed (%d)", (unsigned)i + 1U, level);
            return level;
        }
        int active = level ^ !!(spec->dt_flags & GPIO_ACTIVE_LOW);
        shell_print(shell,
            "DI%u %s pin=%u level=%d active=%d",
            (unsigned)i + 1U,
            spec->port->name,
            spec->pin,
            level,
            active);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(inputs_commands,
    SHELL_CMD_ARG(
        show, NULL, "Show digital input levels and active states.", cmd_inputs_show, 1, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(inputs, &inputs_commands, "Input diagnostics.", NULL);
#endif
