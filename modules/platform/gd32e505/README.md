# GD32E505 硬件支持

本模块补充 当前 Zephyr 缺少的 E505 SoC 接入、可配置时钟启动和 ADC0 驱动，
复用 West 管理的 `hal_gigadevice` 与 Zephyr GD32 公共代码，不复制厂商固件库。

```text
gd32e505/
├── zephyr/module.yml       # soc_root、dts_root、CMake 和 Kconfig
├── soc/gd/gd32e505/         # SoC 元数据和早期时钟初始化
├── dts/arm/gd/             # 复用上游 E50x，补充 ADC0 节点
├── dts/bindings/           # PLL 和 ADC 设备树约束
├── include/dt-bindings/    # E505 LQFP144/100/64 引脚复用定义
├── pinconfigs/             # gd32pinctrl.py 的 E505 引脚配置源
├── drivers/adc/            # Zephyr ADC API 的同步后端
└── drivers/serial/         # 标准 UART async 的 DMA 收发扩展
```

## 接入

在 `find_package(Zephyr ...)` 前将此目录加入 `ZEPHYR_EXTRA_MODULES`，
或指定 `-DEXTRA_ZEPHYR_MODULES=/absolute/path/to/modules/platform/gd32e505`。
元数据提供 `SOC_ROOT`、`DTS_ROOT`；板级 DTS 包含 `arm/gd/gd32e505.dtsi`。
[`product/cmake/hardware.cmake`](../../../product/cmake/hardware.cmake) 供本应用和 Sysbuild 共用，
[`product/cmake/modules.cmake`](../../../product/cmake/modules.cmake) 另注册点表及算法模块。

[`bootloader/CMakeLists.txt`](../../../product/bootloader/CMakeLists.txt) 在镜像发现前注册硬件模块，
并通过缓存把列表传给 MCUboot 和应用。芯片源码仅在选择 `gd32e505` 时编译；
ADC 驱动仅在 `ADC=y`、ADC 节点启用且 `ADC_GD32E50X=y` 时编译。
其他板型的模拟器测试可以继续使用同一模块清单。

## 引脚复用

`include/dt-bindings/pinctrl/gd32e505xx-afio.h` 定义 E505 的 AFIO_PCF0/PCF1
重映射字段；`gd32e505z(e-c)xx-pinctrl.h`、`gd32e505v(e-c)xx-pinctrl.h` 和
`gd32e505r(e-c)xx-pinctrl.h` 分别覆盖 LQFP144、LQFP100 和 LQFP64，按数据手册
表 2-3、2-4、2-5 生成。所有文件均不引用 E507 板级头文件。

引脚配置源为 `pinconfigs/gd32e505xx.yml`，使用模块自有生成器：

```bash
python3 modules/platform/gd32e505/scripts/generate_pinctrl.py
python3 modules/platform/gd32e505/scripts/generate_pinctrl.py --check
python3 modules/platform/gd32e505/tests/test_pinctrl.py
```

生成器需要 PyYAML。选择值以 West 固定版本官方 HAL 的
`gd32e50x_gpio.h` 中 `AFIO_Px_FUNCTION_CFG` 为依据；引脚与封装以数据手册
Rev2.3 表 2-3～2-5 为依据。重映射数组中的空槽保留硬件编号，I2S2 使用 SPI2 字段。
不要直接使用上游生成器覆盖本模块头文件，它不处理 E505 的 Alternate1 选择。

新增 `gd32e505{z,v,r}{e,c}xx-pinctrl.h` 分型头文件；C 型号排除仅 E 型号具备的
TIMER7～13 功能。原 `(e-c)` 头文件保留功能并集兼容，不能用于检查 C 型号能力；
新板应选择精确型号头文件。B 型号尚未提供独立头文件。

`drivers/pinctrl/pinctrl_gd32e505.c` 基于当前 Zephyr AFIO 后端，保留其许可证，
补充每个引脚的 PCF0/PCF1 重映射及 PCFA～PCFG 选择。pinmux 的 bit20～21
保存 Alternate1 选择值，其余编码与上游一致。零值清除该引脚选择字段，恢复
Alternate2/普通 GPIO 路径；模拟输入同样清除该字段。比较器模拟输入使用 ANALOG。
本 SoC 默认关闭上游 AFIO 后端，避免重复定义。不要同时手动启用
`PINCTRL_GD32_AFIO`。板级必须避免同时启用互相冲突的重映射组。

验证覆盖全部九个头文件的宏展开与 C 编译、生成一致性和关键路由。
构建通过不代表 SHRTIMER、CAN2、USB HS 等外设已完成实板验证。

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
[`app/acquisition/acquisition.c`](../../../product/app/acquisition/acquisition.c) 装配产品配置，直接组合模拟量和 GPIO 读取：
通道顺序 13、0、1、2、3、4、11、12，每通道 10 次整数平均，12 位、3.3 V 及
71.5 周期采样时间与原配置相同。工程量换算位于 `app/acquisition/acquisition.c`。

## 板级与启动程序

PC13 外部看门狗归 [`board.c`](../../../product/boards/idu/idu_pre/board.c)，
板级选择 `BOARD_EARLY_INIT_HOOK`，在 SoC 初始化之后、`PRE_KERNEL_1`
之前释放 JTAG、保留 SWD；仅 MCUboot 将 PC13 设为浮空。
应用通过 `modules/func/watchdog` 按产品设备树配置启动外部喂狗。
使用 `GD32E50X_CL`，保留 FMC 字对齐编程兼容定义。
启动代码断言 PLLMF[4] 位于 bit29，拒绝误选 XD 的 bit27 寄存器布局。

`sysbuild/` 保留标准入口，镜像和分区策略在 `bootloader/`。
自定义 MCUboot overlay 会被保留；调用者需自行包含必要的启动分区布局。

依据：[Zephyr 模块设置](https://docs.zephyrproject.org/latest/develop/modules.html#build-settings)、
[ADC API](https://docs.zephyrproject.org/latest/doxygen/html/group__adc__interface.html)、
[GD32E505 数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)
表 4-20、4-30。实际软件行为以工作区锁定的源码为准；
[测试说明](../../../docs/testing.md)区分桌面测试和实板验证。

## 标准 UART async 扩展

启用 `UART_GD32_ASYNC` 时，以本模块 `drivers/serial/usart_gd32.c` 替换构建中的
上游 UART 实现，保留原版权；West 上游文件不修改。扩展实现在 `usart_gd32_async.inc`，
对外提供标准 callback_set/tx/tx_abort/rx_enable/rx_buf_rsp/rx_disable，删除了自定义适配接口。
驱动实例保留原 poll、interrupt 和运行期配置能力；同一实例使用 async 时不得混用 interrupt 回调。

TX DMA 从 UART 的 dmas/tx 描述符取得，USART2 为 DMA0 CH1。
DMA 完成仅开启 TC 通知，USART TC 后报告 UART_TX_DONE；中止停止请求和通道后报告
UART_TX_ABORTED，事件 len 是已交给外设的数量，不保证已完整出线。
支持有限 TX timeout（系统延时工作处理）和 SYS_FOREVER_US，单次 1–65535 字节。
有 DMA 的 UART 在 POST_KERNEL、DMA 控制器之后初始化；无 DMA 的 console 保留 PRE_KERNEL。

RX DMA 从 UART 的 dmas/rx 描述符取得，USART2 为 DMA0 CH2；只在 USART0/1/2 上启用。
实现位于 `usart_gd32_rx.inc`，使用普通 DMA 模式，不使用循环覆盖：
缓冲填满后停止旧传输、交付增量 RX_RDY、启动后备缓冲、释放旧缓冲并请求下一块。
没有后备缓冲时报告 RX_DISABLED；恢复须重新提供缓冲，不创建内部字节环或接收线程。
上游 GD32 DMA 驱动仅提供普通传输完成通知，不支持此处所需的循环 HT/FT 接口，
因此本实现不依赖半传输中断，也不修改 West 上游 DMA 驱动。

短包用硬件接收空闲超时 RTF 通知。参考手册 Rev2.2 第 707–708 页规定 RTF 可写零清除，
不会像 IDLEF 的 STAT0/DATA 清除序列那样与 DMA 争读 DATA。RX DMA 不使能 RBNE 或 IDLE 中断。
RX timeout 单位为微秒：0 使用硬件最小一个波特时钟，不能解释为一字符空闲；
正值向上取整为波特时钟，最大阈值 0xfffffe，超出返回 -ENOTSUP；
SYS_FOREVER_US 不产生空闲通知，只在满缓冲或停止时交付。单块 1–65535 字节。
未配置 RX DMA 时保留逐字节中断 async RX，仅支持 timeout=0。

暂停／停止先关闭 USART DMA 请求，再停止 DMA、快照剩余计数；报告从上次 offset 后的新数据。
成功停止后才释放缓冲；stop/status 错误保留所有权并允许重试 disable。
停止时收取已就绪的残留字节，重启时丢弃关闭期间的 DATA；UART 错误按 STAT0/DATA 清除，
报告 RX_STOPPED、有效前缀及缓冲区释放／关闭事件。DMA 错误按潜在数据丢失报告 OVERRUN。
TX 终止不强制交付 RX 数据，避免破坏接收超时契约。发送时仅按事件状态过滤，
延迟交付的本机回显仍可能泄漏；必须验证收发器 /RE 门控、最后停止位和立即响应。
不能把软件过滤当作硬件接收门控。

收发期间运行期重配置返回 -EBUSY；同一 UART 的 TX/RX 不允许占用同一个 DMA 通道。
实板 DMA、缓冲切换窗口、DE、接收超时及错误恢复尚待验证。

平台覆写 UART 的 poll_in 无数据返回 -1，遵循标准轮询契约；RS485 不增加 GD32 专用返回值兼容。

启用 UART_ASYNC_API 时默认启用 UART_GD32_ASYNC，并选择 UART_INTERRUPT_DRIVEN；
未启用 DMA 后端或手动关闭扩展会在构建期失败，避免运行期缺少 async ops。
通用 RS485 不依赖中断 API；上述依赖仅属于本 GD32 驱动实现。
CMake 检查上游源码 SHA256、串口目标及唯一源文件，Zephyr 升级后必须先复核并合并
上游差异再更新哈希。该检查只提示基线变化，不自动合并修复。

## 标准 PWM 输入捕获

`CONFIG_PWM_CAPTURE_GD32` 根据启用的 `gd,gd32-pwm-capture` 节点选择，实现标准
`pwm_configure_capture()`、`pwm_enable_capture()`、`pwm_disable_capture()` 和计数时钟查询。
驱动、IRQ、寄存器和 binding 全部位于本模块，不修改 West 上游 PWM 驱动。
设备节点作为 `gd,gd32-timer` 的子节点，继承父定时器时钟、reset、prescaler 和 IRQ。

当前仅支持具有独立 up/cc IRQ 的 16 位高级定时器 CH0：连续/单次周期捕获，
正常或反向极性选择上升/下降沿，不支持脉宽捕获或输出。当前产品使用 TIMER0 CH0 全重映射 PE9。
捕获设备独占父定时器，不能同时打开它的 PWM 输出或 GD32 counter 驱动。
回调在 ISR、驱动锁外执行；运行期停止/切换回调须由调用方保证 context 生命周期。
子节点的 `capture-prescaler` 支持 1/2/4/8，默认 1；产品设为 8，硬件每 8 个边沿捕获一次。
驱动以右移将跨多个边沿的计数归一化为标准单周期，ISR 不做频率或 RPM 除法。
硬件分频仅降低捕获中断，不影响定时器的更新中断频率；低速信号必须相应扩大上层超时。

1 MHz 计数下每 65.536 ms 溢出，通过更新 IRQ 扩展时间戳；溢出与捕获同时挂起时按 CV 高低
半区判定先后，要求 IRQ 响应小于 32.768 ms，不能保证长时间关中断后的正确性。
overcapture 返回 `-EOVERFLOW` 并重新同步，超出标准 32 位周期范围返回 `-ERANGE`。
该驱动已通过真实产品构建；PE9 波形、IRQ 时序和最大输入负载仍待实板验证。

### 捕获超时诊断

板级调试可启用 `CONFIG_PWM_CAPTURE_GD32_DIAGNOSTICS=y`（默认关闭，当前产品也关闭）。
在线程停止未完成捕获时打印 `Capture diagnostic`、`Capture clocks` 和 `Capture pin`：
停止前的 CNT/CTL0/INTF/DMAINTEN/CHCTL0/CHCTL2、计数频率、PSC/CAR、
更新及捕获 IRQ 次数、NVIC 使能状态、APB2 时钟/复位、AFIO 重映射和配置输入的 GPIO 模式。
日志和 16 次间隔 100 us 的被动 GPIO 采样均在 spinlock 外、线程上下文执行。
输入 GPIO 地址按 E50x GPIOA 起始地址及 0x400 端口间隔取得，不改变引脚配置。

当前 TIMER0/PE9 的预期配置：rate=1000000、PSC=179、CAR=65535、CTL0.CEN=1、
DMAINTEN.UPIE/CH0IE=1、CHCTL0=0x0d（直接输入、8 分频）、CHCTL2.CH0EN=1；
APB2EN 的 TIMER0/GPIOE/AFIO 位应置位，APB2RST.TIMER0RST 应清零，
AFIO_PCF0.TIMER0_REMAP 应为 3，PE9 模式为下拉输入（0x8）。
无输入的 500 ms 测量通常有约 7 次更新 IRQ；1 kHz 单次测量应在约 16 ms 内完成。
`transitions_1600us` 仅表示短时软件采样是否观察到输入电平变化，不代替示波器测量，
高频或与采样间隔同步的信号可能被混叠。排障完成后关闭诊断配置。


### 标准定时器时钟查询修正

固定版本上游 `clock_control_gd32_get_rate()` 使用 `psc != 1` 判断 APB 定时器倍频，
但 `psc` 是寄存器字段编码：0..3 均表示不分频，4..7 表示 /2、/4、/8、/16。
当前 APB2 编码为 0，旧实现因此将 180 MHz 错报为 360 MHz；PSC=179 时
标准 capture API 报告 2 MHz，而实际更新 IRQ 和 CNT 表明计数约为 1 MHz。

`cmake/clock_control.cmake` 校验上游 SHA256，在构建目录生成只修正该判断的副本，
替换时钟目标中唯一对应源文件；不修改 West 源码。上游升级后必须复核再更新基线。
应用仍使用标准 PWM/clock API，不在 fan 或 Shell 里补偿频率。
`tests/test_gd32_timer_clock.py` 编译实际查询函数，对两个 APB 的全部字段编码及
普通外设频率进行验证。此修正不解释 PE9 没有边沿，捕获超时仍需排查输出路径。

### 标准 PWM 输出 CAR 边界修正

固定版本上游 `pwm_gd32.c` 把 `period_cycles` 直接写入 `TIMER_CAR`，但 GD32 的实际
PWM 周期为 `CAR + 1` 个计数；请求 1 kHz 时实际为 1001 计数（999 Hz）。
`cmake/pwm_gd32.cmake` 校验上游 SHA256，在构建目录生成把 CAR 写为
`period_cycles - 1` 的副本，并允许 16 位定时器使用 `period_cycles=65536`
（CAR=65535）；不修改 West 源码。上游升级后必须复核并更新哈希。
`tests/test_gd32_pwm_period.py` 验证补丁文本和 16 位边界。

排障期间产品启用标准 `CONFIG_DEVMEM_SHELL`；例如只读查看 PA0 和当前 TIMER1：

```text
devmem 0x40010800 32
devmem 0x40010808 32
devmem 0x4002101c 32
devmem dump -a 0x40000000 -s 64 -w 32
```

以上依次为 GPIOA_CTL0、GPIOA_ISTAT、RCU_APB1EN 和 TIMER1 寄存器块。
不使用 devmem 写寄存器代替驱动修复。
