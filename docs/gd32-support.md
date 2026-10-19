# GD32E505 官方支持与 IDU-pre 适配边界

核查日期：2026-10-19。范围为兆易创新官网、其官网确认的 GitHub 组织、Zephyr 官方仓库与本工作区；结论来自源码和文档检查，未代替实板时钟、ADC、Flash 或升级验证。

**结论：官方确实提供 GD32 固件库，也提供 Zephyr 下游仓库；当前 IDU-pre 已经复用 Zephyr 的 `hal_gigadevice`。但上游和官方下游均未登记 GD32E505 SoC，且默认启动程序不适配本板的 20 MHz 有源时钟。因此不能通过添加一个官方模块就删除 `soc/idu/gd32e505/`。** 合理方向是继续复用官方 HAL 和现有 Zephyr 驱动，仅保留 E505、本板时钟和尚未覆盖的 ADC 适配。

**官方资源分为三层。** 兆易创新在 [官方 GitHub 介绍](https://www.gigadevice.com/about/blog/gd32-mcu-official-github) 中确认 `GigaDevice-GD32-MCU` 和 `GD32-MCU-IOT` 两个组织；[官方 Zephyr/IAR 指南](https://www.gigadevice.com/about/blog/zephyr-gd32-iar-guide) 也直接使用后者的 `gd32_zephyr`。

| 层次 | 已核实的提供内容 | 对本项目的意义 |
| --- | --- | --- |
| 厂商固件库、CMSIS、开发包 | GD32E50x Firmware Library、AddOn、Demo Suites；覆盖 E505 等 E50x 器件 | 提供寄存器定义、外设函数、启动范例和工具支持；不是完整 Zephyr 板级移植 |
| Zephyr 上游 HAL 模块 | `zephyrproject-rtos/hal_gigadevice`，含 `gd32e50x` | 本工作区已经通过 West 导入；无需再复制一份固件库 |
| 兆易创新 Zephyr 下游 | `GD32-MCU-IOT/gd32_zephyr` 和 `hal_gigadevice_zephyr` | 是完整 Zephyr 分支和配套 HAL，不能视为可直接叠加到任意 Zephyr 版本的小模块 |

[官方 SDK 总索引](https://github.com/GigaDevice-GD32-MCU/GD32-MCU-SDK) 中，E50x 的 AddOn、Demo Suites 有独立仓库链接，但 Firmware Library 栏当前为 `—`。因此固件库以官网发布目录为入口，不臆造 `GD32E50x_Firmware_Library` GitHub 仓库。

**版本和覆盖范围。** 本地 `manifest/west.yml` 固定 `v4.4.2`，Zephyr 的导入清单固定 HAL 提交。网络 `main` 会变化，以下记录便于后续复核。

| 对象 | 本次核实版本 / 提交 | E50x 集成情况 |
| --- | --- | --- |
| 本地 Zephyr | `v4.4.2` | `SOC_GD32E507`；无 `SOC_GD32E505` |
| 本地 HAL | `ee0e31302c21b2a465dc303b3ced8c606c2167c8` | 已有 E50x CMSIS、RCU、GPIO、ADC、FMC 等源文件 |
| Zephyr 上游 `main` | `a915b2f12675b48b16d56653ea145dcd8f20cbbf`，`VERSION` 为 `4.5.0-rc2` | `soc.yml` 中 E50x 仍只有 `gd32e507` |
| 上游 `main` 导入 HAL | `804f92bf5794e403fab89178dcc6d775b9269c0d` | 更新依赖也不自动补齐 E505 SoC 和本板启动 |
| 厂商 Zephyr `main` | `9a6008dfafc339fedd3a47611adc2fbc58c19027`，`VERSION` 为 `4.4.99` | E50x 有 `gd32e507`、`gd32e508`，仍无 `gd32e505` |
| 厂商下游导入 HAL | `hal_gigadevice_zephyr`，清单 `revision: main` | 浮动分支；若未来采用，需要另行锁定提交并验证整套依赖 |

覆盖范围可直接核对 [上游 SoC 清单](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/soc/gd/gd32/soc.yml)、[厂商 SoC 清单](https://github.com/GD32-MCU-IOT/gd32_zephyr/blob/9a6008dfafc339fedd3a47611adc2fbc58c19027/soc/gd/gd32/soc.yml) 和 [厂商 E50x Kconfig](https://github.com/GD32-MCU-IOT/gd32_zephyr/blob/9a6008dfafc339fedd3a47611adc2fbc58c19027/soc/gd/gd32/gd32e50x/Kconfig.soc)。上游现成开发板是 [GD32E507Z-EVAL](https://docs.zephyrproject.org/latest/boards/gd/gd32e507z_eval/doc/index.html)，不是本板的 E505。

**为什么目前仍需要自己的 `soc.c`。** 本地 [SoC 启动](../soc/idu/gd32e505/soc.c)、[构建配置](../soc/idu/gd32e505/CMakeLists.txt) 和 [板级 DTS](../boards/idu/idu_pre/idu_pre.dts) 已复用官方类型、HAL 函数、上游 E50x DTS 与 GD32 公共目录，补充以下硬件条件：

- `GD32E50X_XD` 类型选择、`HXTAL_VALUE=20000000`，并断言 CPU 为 180 MHz。
- OSC_IN 输入是 20 MHz 有源时钟，需要 HXTAL bypass；PLL 为 `20 / 2 × 18 = 180 MHz`。
- 兼容旧引导程序已经开启 PLL 的情况，先切换并确认 IRC8M，再复位时钟配置。
- Flash 等待周期、APB 分频、高驱动模式、时钟稳定等待，以及 MCUboot 期间 PC13 外部看门狗的板级处理。

上游和厂商下游 E50x 的 `soc_early_init_hook()` 都仅调用 `SystemInit()`。两套 HAL 的默认 `SystemInit()` 均清除 `HXTALBPS`；其 XD/HD 的 180 MHz 分支使用固定 `HXTAL / 2 × 45`，针对默认 8 MHz 输入。因此只把 `HXTAL_VALUE` 改成 20 MHz 不会重算 PLL，会尝试配置 `20 / 2 × 45 = 450 MHz`，也会丢失 bypass 设置。该结论由源码公式推导，不代表实测频率。[上游启动源码](https://github.com/zephyrproject-rtos/hal_gigadevice/blob/804f92bf5794e403fab89178dcc6d775b9269c0d/gd32e50x/cmsis/gd/gd32e50x/source/system_gd32e50x.c)、[厂商 HAL 启动源码](https://github.com/GD32-MCU-IOT/hal_gigadevice_zephyr/blob/main/gd32e50x/cmsis/gd/gd32e50x/source/system_gd32e50x.c)。

上游 [HAL CMake 接入](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/modules/hal_gigadevice/CMakeLists.txt) 为 E507 选择 `GD32E50X_CL`，晶振配置分支为 8/25 MHz；厂商下游增加 E508 选择，但仍未提供本板的 20 MHz bypass 配置。也不应把 E505 假报为 E507 来绕过 SoC 登记。

当前适配可以继续收敛：用现有 FMC/PMU/RCU HAL API 表达可替代的寄存器操作，将 PC13 的板级策略单独归置，并保留必要等待和引导兼容顺序。但这些属于后续硬件变更，需要冷启动、旧 Boot 跳转和 MCUboot 升级验证；本次研究没有改动启动代码。

**哪些可以直接使用 Zephyr 接口。** 判断依据应同时包括 HAL、Zephyr 驱动和 DTS 集成，不能仅凭目录里存在 `gd32e50x` 推断每个外设都已可用。

| 功能 | 当前已有能力 | IDU-pre 的使用和剩余边界 |
| --- | --- | --- |
| GPIO / pinctrl | GD32 GPIO 驱动和引脚控制 | `io.c`、`health.c` 已使用 `gpio_*_dt`；引脚映射、JTAG/SWD 复用和外部看门狗仍属板级配置 |
| UART / Modbus RTU | Zephyr GD32 UART 与 Modbus 子系统 | 通过 DTS、端口配置和 Modbus 接口使用；不需重写串口 HAL |
| SPI / SPI NOR | GD32 SPI 与通用 SPI NOR 驱动 | 本板补充 SPI1 DTS 节点，使用 `gd,gd32-spi`；外部 Flash 由现有存储驱动承接 |
| 内部 Flash / 分区 / 升级 | GD32 FMC v1、Flash Map、NVS、`flash_img` | 应继续使用现有接口；E505 写入对齐、本板分区和 Boot 兼容需要保留 |
| ADC | E50x 官方 ADC HAL 已有；通用 `adc_gd32.c` 也存在 | E50x 的 DTS 和驱动初始化分支尚不完整，当前 `analog.c` 以 HAL 完成适配 |
| 系统时钟 | 官方 RCU/PMU/FMC HAL；Zephyr GD32 公共时钟接口 | 20 MHz bypass 和启动顺序尚需本地 SoC 早期初始化 |

Flash 还有一处具体兼容原因：当前 [上游 `flash_gd32_v1.c`](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/drivers/flash/flash_gd32_v1.c) 定义 `GD32_FMC_V1_WORK_ALIGNED`，后续分支判断却是 `FLASH_GD32_FMC_WORK_ALIGNED`。本地 CMake 显式定义后者以选择 E505 所需的字写入路径，不能在没有替代修复和 Flash 验证时顺手删除。

**ADC 的准确支持状态。** [上游 ADC Kconfig](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/drivers/adc/Kconfig.gd32) 没有显式排除 E50x；`ADC_GD32` 依赖的是 `DT_HAS_GD_GD32_ADC_ENABLED`。缺口在实际集成：

- 上游和厂商下游的 `gd32e50x.dtsi` 均没有 ADC 节点。
- 两者 `adc_gd32.c` 的 ADC 时钟分频、分辨率寄存器和软件触发初始化条件都未覆盖 `CONFIG_SOC_SERIES_GD32E50X`。
- 厂商下游增加了 DMA 和其他系列分支，但仍未补齐上述 E50x 条件；不能把新增 DMA 等同于 E505 支持。
- 驱动仍限制一次序列只选择一个通道；校准等待循环也没有本地适配目前使用的超时边界。

证据：[上游 ADC 驱动](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/drivers/adc/adc_gd32.c)、[厂商 ADC 驱动](https://github.com/GD32-MCU-IOT/gd32_zephyr/blob/9a6008dfafc339fedd3a47611adc2fbc58c19027/drivers/adc/adc_gd32.c)、[上游 E50x DTS](https://github.com/zephyrproject-rtos/zephyr/blob/a915b2f12675b48b16d56653ea145dcd8f20cbbf/dts/arm/gd/gd32e50x/gd32e50x.dtsi)、[厂商 E50x DTS](https://github.com/GD32-MCU-IOT/gd32_zephyr/blob/9a6008dfafc339fedd3a47611adc2fbc58c19027/dts/arm/gd/gd32e50x/gd32e50x.dtsi)。

本地 [analog.c](../src/analog.c) 已调用 `gd32_adc.h` 的官方函数配置 APB2/6、12 位、单通道软件触发，按板级通道顺序采样并平均，校准和转换分别设置等待期限。这是对现有 HAL 的适配。后续若要求业务统一调用 `adc_channel_setup()` / `adc_read()`，可以在应用自有驱动目录封装 E505 HAL，或补齐并向上游提交通用驱动支持；均需验证分频、触发、校准、通道顺序、错误恢复及实际电流换算。

**本次依赖选择建议。** 保持 `v4.4.2` 及现有固定 HAL 提交。本次所需的 GPIO/UART/SPI/Flash 接口已经存在；厂商下游尚不能消除 E505 时钟和 ADC 缺口，切换整个 Zephyr 分支没有与之对应的直接收益。后续需要某项厂商修复时，先比较具体提交、锁定依赖、单独构建验证；不直接复制整个固件库，也不让生产清单追踪浮动 `main`。本研究没有修改 West 清单或任何上游文件。

**官方下载与源码索引。** 官网不同入口可能同步滞后，以下软件包版本和发布日期取 `gd32mcu.com` 的 E50x 分类目录；下载时再次核对版本和发行说明。旧 API 手册可说明接口，不能据其标题推断当前固件包版本。

| 资源 | 本次检索结果与用途 | 官方入口 |
| --- | --- | --- |
| GD32E50x Firmware Library | 1.7.0，2026-02-11；E50x CMSIS、外设驱动和示例，覆盖 E505 | [固件库目录](https://www.gd32mcu.com/cn/download/7?kw=GD32E5) |
| GD32E50x AddOn | 1.9.0，2026-04-15；Keil/IAR 器件开发包，非 Zephyr SoC 移植 | [官网目录](https://www.gd32mcu.com/cn/download/7?kw=GD32E5)、[官方仓库](https://github.com/GigaDevice-GD32-MCU/GD32E50x_AddOn) |
| GD32E50x Demo Suites | 1.8.0；参考 E503/E507 等官方评估板例程，不能直接替代 IDU 板级配置 | [官方仓库](https://github.com/GigaDevice-GD32-MCU/GD32E50x_Demo_Suites) |
| GD32E505 数据手册 | 2.3，2026-04-15；器件、电气参数、引脚和存储规格 | [目录](https://www.gd32mcu.com/cn/download/5?kw=GD32E5)、[下载入口](https://www.gd32mcu.com/download/down/document_id/251/path_type/1) |
| GD32E50x User Manual | 2.2，2026-04-15；E503/E505/E507/E508/EPRT 外设寄存器说明 | [目录](https://www.gd32mcu.com/en/download/6?kw=GD32E5)、[英文下载](https://www.gd32mcu.com/download/down/document_id/249/path_type/1) |
| GD32E50x Errata Sheet | 1.6，2025-10-13；变更驱动和时钟前应对照的已知限制 | [目录](https://www.gd32mcu.com/en/download/11?kw=GD32E5)、[英文下载](https://www.gd32mcu.com/download/down/document_id/437/path_type/1) |
| Firmware Library User Guide | Rev1.3，文档内日期为 2021-12；HAL API 参考，不代表 1.7.0 发布说明 | [官方 PDF](https://www.gd32mcu.com/data/documents/userManual/GD32E50x_Firmware_Library_User_Guide_Rev1.3.pdf) |
| 兆易创新 SDK 总索引 | 官方各系列固件、AddOn、Demo 仓库索引 | [GD32-MCU-SDK](https://github.com/GigaDevice-GD32-MCU/GD32-MCU-SDK) |
| Zephyr 上游 HAL | 本项目实际使用的依赖来源 | [hal_gigadevice](https://github.com/zephyrproject-rtos/hal_gigadevice)、[本地对应提交](https://github.com/zephyrproject-rtos/hal_gigadevice/commit/ee0e31302c21b2a465dc303b3ced8c606c2167c8) |
| 厂商 Zephyr 下游 | 完整 Zephyr 分支及其依赖清单 | [gd32_zephyr](https://github.com/GD32-MCU-IOT/gd32_zephyr)、[west.yml](https://github.com/GD32-MCU-IOT/gd32_zephyr/blob/9a6008dfafc339fedd3a47611adc2fbc58c19027/west.yml) |
| 厂商下游 HAL | 与上述分支配套的 HAL，不保证可独立替换本地固定版本 | [hal_gigadevice_zephyr](https://github.com/GD32-MCU-IOT/hal_gigadevice_zephyr) |

本地复核命令：`git -C zephyr describe --tags --always`、`git -C modules/hal/gigadevice rev-parse HEAD`，并检查 `manifest/west.yml`、`zephyr/west.yml`、`zephyr/soc/gd/gd32/gd32e50x/`。本报告仅新增文档；没有进行依赖升级、修改 SoC/ADC，或声称新的实板验证已经完成。
