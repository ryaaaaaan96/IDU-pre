# 指示灯统一使用 GPIO

Status: resolved

## 用户决定与实现

2026-10-24：用户要求指示灯统一改用 GPIO，并删除 `output()` 包装。

- `status.c` 从设备树 `led0` 的 gpios 获取引脚和有效极性。
- `status_init()` 仅检查一次 GPIO 就绪，并以 `GPIO_OUTPUT_ACTIVE` 配置启动常亮。
- 删除 `indicator_on` 与 `output()`。运行时到期后，按错过周期的奇偶决定是否执行
  `gpio_pin_toggle_dt()`；偶数周期不翻转，也不补放历史边沿。
- 终止故障使用逻辑值 1 强制点亮；翻转后再次检查故障阶段，避免本次运行态输出覆盖
  同时发生的故障常亮要求。
- 仅保留初始化成功标志。未就绪或配置失败后，若故障策略允许继续，工作项跳过
  后续指示灯访问，仍执行看门狗错误检查。
- 应用移除 `CONFIG_LED` / `CONFIG_LED_GPIO` 和必需 LED 服务检查。设备树的
  gpio-leds 描述继续表达硬件引脚，应用直接使用 GPIO；实际配置确认 LED 驱动未启用。
- 测试改为可注入错误的 GPIO 驱动，保留真实的 GPIO 有效极性转换；不通过 LED 接口
  模拟输出。运行测试同时覆盖初始化和后续输出失败。

本轮开始前已有 `status.c` 初始化锁位置调整。本轮保留该工作区改动，只提交 GPIO
部分；修改前补丁为工作区 `build/IDU-pre/gpio-indicator/user-status.patch`。
构建与测试使用当前工作区，包含该既有改动。

## 验证

| 验证 | 结果 |
| --- | --- |
| acquisition native 构建及 run.py | 36 个独立进程场景通过 |
| task_watchdog native 构建及 run.py | 12 个独立进程场景通过 |
| `tests/runtime/run.sh` | 24 个真实装配场景通过 |
| `tests/test_build_configuration.py` | 8 项通过 |
| `CCACHE_DISABLE=1 ./scripts/build.sh` | Arm MCUboot + IAPv3 构建、验签及导出通过 |
| diff 空白检查 | 通过 |

覆盖低电平点亮及物理翻转、正常独立闪烁、错过奇数／偶数周期、GPIO 未就绪时无后续
访问、初始化失败、采样阻塞、故障常亮及原始故障保留。真实装配覆盖复位、留驻、调试、
LED 继续和存储降级；LED 配置失败且允许继续时，采样持续推进且无后续灯引脚写入。

Arm 应用 `.config` 为 `CONFIG_GPIO=y`、`CONFIG_LED` 未设置；编译数据库不含
`led_gpio.c`。引脚仍为设备树配置的 PA8、低电平有效，本轮未改产品引脚或极性。

native 运行设置 `ASAN_OPTIONS=detect_leaks=0`，不作为泄漏验证。没有运行全仓回归、
下载实板或测量灯波形；实物极性、时序和故障常亮仍需实板确认。
日志位于工作区 `build/IDU-pre/gpio-indicator/`。
