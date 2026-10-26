# GD32E505 硬件支持

本模块补充 Zephyr 4.4.2 缺少的 E505 SoC 接入、可配置时钟启动和 ADC0 驱动，
复用 West 管理的 `hal_gigadevice` 与 Zephyr GD32 公共代码，不复制厂商固件库。

```text
gd32e505/
├── zephyr/module.yml       # soc_root、dts_root、CMake 和 Kconfig
├── soc/gd/gd32e505/         # SoC 元数据和早期时钟初始化
├── dts/arm/gd/             # 复用上游 E50x，补充 ADC0 节点
├── dts/bindings/           # PLL 和 ADC 设备树约束
└── drivers/adc/            # Zephyr ADC API 的同步后端
```

## 接入

在 `find_package(Zephyr ...)` 前将此目录加入 `ZEPHYR_EXTRA_MODULES`，
或指定 `-DEXTRA_ZEPHYR_MODULES=/absolute/path/to/modules/gd32e505`。
元数据提供 `SOC_ROOT`、`DTS_ROOT`；板级 DTS 包含 `arm/gd/gd32e505.dtsi`。
[`../hardware.cmake`](../hardware.cmake) 供本应用和 Sysbuild 共用，
[`../modules.cmake`](../modules.cmake) 另注册点表及算法模块。

[`bootloader/CMakeLists.txt`](../../bootloader/CMakeLists.txt) 在镜像发现前注册硬件模块，
并通过缓存把列表传给 MCUboot 和应用。芯片源码仅在选择 `gd32e505` 时编译；
ADC 驱动仅在 `ADC=y`、ADC 节点启用且 `ADC_GD32E50X=y` 时编译。
其他板型的模拟器测试可以继续使用同一模块清单。

## 时钟配置

板级 DTS 定义 `hxtal` 固定时钟节点，以及一个 `gd,gd32e505-pll` 节点。
PLL 的 `clocks` 引用该输入，`hxtal-bypass` 表示有源输入，未设置时使用晶振。
`prediv`、`mul`、`apb1-prescaler`、`apb2-prescaler` 描述分频和倍频。
CPU 节点的 `clock-frequency` 必须与计算结果一致；HAL 的 `HXTAL_VALUE`
由同一 `hxtal` 节点生成。

当前板使用 20 MHz bypass、PLL `/5 ×45`、AHB/APB2 180 MHz、APB1 90 MHz，
与已在本板正常打印的 `GD32E505_Firmware` 工程配置一致。
支持边界为 HXTAL 4–32 MHz、预分频 1–16、整数倍频 2–63、PLL 输入 2–16 MHz、
输出/CPU 16–180 MHz，APB1 不超过 90 MHz，APB2 不超过 180 MHz。
AHB 固定为 `/1`，APB 分频可选 1/2/4/8/16；不提供 IRC、PLL1/2 或 USB 时钟方案。
不合法的倍频、总线超频和 CPU 频率不一致会在构建时被拒绝。

启动先切换 IRC8M，再重配 PLL，安全接管启动程序可能已启用的 PLL。
当前保持保守的 4 个 Flash 等待周期和高驱动模式，不按频率优化功耗。
时钟等待失败时停止启动；板级复位与故障恢复策略另行处理。
8 MHz 晶振 / 120 MHz CPU 的配置只经过编译验证，不代表实板验证。

## ADC 驱动与应用边界

`gd,gd32e50x-adc` 使用标准 `adc-controller.yaml` 通道属性。
板级配置 `clocks`、`resets`、`pinctrl`、`clock-prescaler` 和 `vdda-millivolts`，
应用通过 `io-channels` 引用通道，使用 `adc_channel_setup_dt()`、`adc_read_dt()`。

- 当前支持 ADC0 外部通道 0–15，同步单通道读取、6/8/10/12 位、单位增益、VDD 参考。
- 采样周期用整数 ticks 表示：2/8/14/29/42/56/72/240，分别对应
  1.5/7.5/13.5/28.5/41.5/55.5/71.5/239.5 个 ADC 周期；默认选择 1.5 周期。
- 校准在设备 `POST_KERNEL` 初始化时完成。校准每阶段最多等待 10 ms，
  单次转换最多等待 2 ms；超时关闭 ADC，下次有效读取重新初始化、校准。
- 每设备互斥锁保护通道设置和读取；错误请求在硬件转换前被拒绝，失败不写结果缓冲区。
- 共享预分频限制为一个 ADC0 实例；多通道序列、额外采样选项、过采样、DMA、
  异步读取和内部温度/VREFINT 通道暂不支持。异步 API 返回 `-ENOTSUP`。
- 预分频可选 2/4/6/8/12/16。根据设备树 VDDA 校验 ADC 时钟：
  1.71 V ≤ VDDA < 2.4 V 时上限为 14 MHz，2.4–3.63 V 时上限为 35 MHz，最低 0.1 MHz。
  该校验依赖板级填写的电压，不测量供电电压。

应用直接通过 Zephyr 标准 ADC API 处理采样与平均，
[`app/acquisition/acquisition.c`](../../app/acquisition/acquisition.c) 装配产品配置，直接组合模拟量和 GPIO 读取：
通道顺序 13、0、1、2、3、4、11、12，每通道 10 次整数平均，12 位、3.3 V 及
71.5 周期采样时间与原配置相同。工程量换算继续留在 `app/process/`。

## 板级与启动程序

PC13 外部看门狗归 [`board.c`](../../boards/idu/idu_pre/board.c)，
板级选择 `BOARD_EARLY_INIT_HOOK`，在 SoC 初始化之后、`PRE_KERNEL_1`
之前释放 JTAG、保留 SWD；仅 MCUboot 将 PC13 设为浮空。
应用通过 `modules/watchdog` 按产品设备树配置启动外部喂狗。
使用 `GD32E50X_CL`，保留 FMC 字对齐编程兼容定义。
启动代码断言 PLLMF[4] 位于 bit29，拒绝误选 XD 的 bit27 寄存器布局。

`sysbuild/` 保留标准入口，镜像和分区策略在 `bootloader/`。
自定义 MCUboot overlay 会被保留；调用者需自行包含必要的启动分区布局。

依据：[Zephyr 模块设置](https://docs.zephyrproject.org/latest/develop/modules.html#build-settings)、
[ADC API](https://docs.zephyrproject.org/latest/doxygen/html/group__adc__interface.html)、
[GD32E505 数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)
表 4-20、4-30。实际软件行为以工作区锁定的源码为准；
[测试说明](../../docs/testing.md)区分桌面测试和实板验证。
