# IDU-pre 硬件与板级适配

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

## 已运行的硬件基准与移植配置

- 用户确认 `io-interface-board` 已在这套实际硬件运行，它是 Zephyr 移植的硬件基准。
  下述上板检查针对 Zephyr 移植，不表示旧硬件方案未经验证。
- 按用户最新说明，MCU 完整型号修正为 **GD32E505VET6**，外部输入为 **20 MHz 有源时钟**。
- 启动使用 HXTAL bypass，PLL 为 `20 MHz / 5 * 45 = 180 MHz`，
  AHB/APB2 为 180 MHz，APB1 为 90 MHz，ADC 时钟为 APB2 / 6 = 30 MHz。
- Zephyr 4.4.2 没有 GD32E505 SoC 定义。本应用在 `modules/gd32e505/soc/gd/gd32e505/`
  补充 SoC 定义，复用上游 E50x CPU、GPIO、串口和 SPI 支持。
  E505 使用 HAL 的 `GD32E50X_CL`，与已正常运行的 `GD32E505_Firmware` 工程一致。
  CL 的 PLLMF 扩展位位于 CFG0 bit29，PREDV0 在 CFG1 配置，不能使用 XD 布局。
- [GD32E505 模块](../modules/gd32e505/README.md) 通过 `soc_root` 向应用和 MCUboot
  提供芯片接入；PLL 来源、bypass、倍频与 APB 分频由板级 DTS 指定，
  HAL 输入频率由同一设备树生成。IDU 引脚与 Flash 分区仍由板及产品配置决定。
  MCUboot 的 PC13 浮空操作在 [板级早期钩子](../boards/idu/idu_pre/board.c) 中，
  于 SoC 时钟初始化后、设备初始化和镜像交换前执行；应用由 `status_init()` 启用喂狗。
  应用和 MCUboot 都在该钩子释放 JTAG、保留 SWD，供 PA15 用作 RS485 DE。
- 用户最新说明的 GD32E505VET6 对应官方手册表 2-1、6-1 的 512 KB Flash、128 KB SRAM、LQFP100，
  与当前容量配置一致；T6 为 -40…+85°C 温度档。调试器器件名称的支持范围另见 [J-Link 方案](wsl-jlink.md)。
- 外部 Flash 按旧代码的 4 MB W25Q32、SPI mode 3 设置；驱动用 SFDP
  检测。待验证的是 Zephyr SPI NOR 驱动的上电、JEDEC/SFDP 和写擦行为。
- 代码中的引脚定义是 Zephyr 移植依据；参考目录未发现原理图。
  功能说明表中有旧的扩展 DI/DO 介绍，与当前代码不完全一致。

### CL 分支修正与上板检查

最初提交 `70a309c` 错选了 `GD32E50X_XD`，迁入硬件模块后继续沿用。
旧检查只验证按同一套 HAL 定义计算的频率，没有验证芯片寄存器布局，因而编译通过。
现在 MCUboot 与应用共用 CL 分支，并采用正常工程的 `20 MHz / 5 ×45` 路径。
新增 bit29 布局断言，在旧 XD 配置下已确认编译失败。

用户捕获的两组 `E0/00` 可通过接收端以发送端六倍波特率解码
`01 04 1F D6 00 02 97 E7`、`02 04 1F D6 00 02 97 D4` 逐字节复现。
同时存在 100 ms 超时约为 607 ms 的现象；这支持时钟偏差判断，但不是硬件实测频率。
修正后需更新包含 MCUboot 的完整 factory 镜像，不能仅用应用升级来验证启动日志。
`scripts/flash.sh` 会擦除内部 Flash 后烧录 factory 镜像，外部 SPI NOR 不擦除。

上板使用 PA9 TX、PA10 RX、115200 8N1 检查两个镜像的启动横幅，
并核对 PA9 单个位宽约为 8.68 µs。可用 GDB 只读检查：

```gdb
x/2wx 0x40021000
x/wx 0x4002102c
x/2wx 0x40013808
x/2wx 0x40004808
```

依次为 RCU CTL/CFG0、CFG1、USART0 BAUD/CTL0、USART2 BAUD/CTL0。
CFG1 的 PREDV0 低四位应为 4（除以 5），PLLPRESEL 和 PREDV0SEL 均为 0；
16 倍过采样时 USART0 BAUD 应为 `0x61b`，USART2 BAUD 应为 `0x30d`。
实际启动日志、采集时序及频率仍须在本板验收。

默认 IAPv3 配置在同一 USART0 启用串口 Shell，提示符 `AIDC:~$ `；
命令见[项目说明](../README.md#串口调试命令)。日志使用 Shell 后端输出，
避免普通 UART 日志与提示符交错。串口工具使用文本模式，命令以 CR 或 CRLF 结束。
SMP 专用 UART 配置关闭串口 Shell 后端，避免两个接收处理器占用 USART0。

## 引脚映射

| 功能 | 引脚或配置 |
| --- | --- |
| DI1–4 | PC6、PC7、PC8、PC9 |
| DI5–8 | PB1、PB2、PB10、PB11 |
| DI9–12 | PE1、PB8、PB9、PE0 |
| DI13–16 | PE2、PE3、PE4、PE5 |
| 拨码地址 bit3–bit0 | PD8、PD9、PD10、PD11 |
| ADC 应用通道 1–8 | PC3、PA0、PA1、PA2、PA3、PA4、PC1、PC2 |
| ADC0 硬件通道顺序 | 13、0、1、2、3、4、11、12 |
| 运行 LED | PA8，低电平亮 |
| RS485 TX / RX | USART2：PC10 / PC11，部分重映射 |
| RS485 DE | PA15，高电平发送，低电平接收；关闭 JTAG，保留 SWD |
| 调试串口 TX / RX | USART0：PA9 / PA10，115200/8N1 |
| SPI Flash CS / SCK / MISO / MOSI | PB12 / PB13 / PB14 / PB15 |
| 外部看门狗 WDI | PC13，每 10 ms 翻转 |
| 可选扩展 I2C SCL / SDA | PC14 / PC15，默认禁用 |

DI 和拨码输入上拉、低电平有效。拨码值 + 1 得到地址 1–16，
PD8 为最高位。寄存器 DI bit0 对应 DI1，bit15 对应 DI16。

当前设备树只声明实际使用的直接 GPIO 输入，已移除未启用的 TCA9535 预留节点。

当前分支的 DO 初始化和输出控制均被注释，部分 DO 引脚与有效 DI
重合，因此本应用不配置继电器输出。旧 NTC 选择引脚 PC1/PC2 也与
当前 ADC 通道重合，本应用仅将其用于模拟输入。

## 已实现行为

- 每 500 ms 获取 16 路 DI 和拨码地址，采集 8 路模拟量。
- 每个模拟通道连续采集 10 次并取整数平均，分辨率 12 bit。
  模块中的 ADC0 驱动通过标准 `adc_*` API 提供逐通道轮询和转换超时，
  应用采样代码按产品配置保留通道顺序及平均。设备初始化时校准，读取超时后下次读取重置并重新校准；
  不沿用 FreeRTOS 的连续 DMA 缓冲区。
  平均数和周期由 `CONFIG_IDU_ADC_AVERAGE_COUNT`、
  `CONFIG_IDU_SAMPLE_PERIOD_MS` 调整初始配置；31000 可设置并保存运行周期。
- 保留当前旧代码的电流换算：
  `raw / 4096 * 3.3 / 0.24 * 50 * 100`，线上的整数单位为 0.01 A。
  旧注释中的“4–20 mA”不能直接当作当前返回值的单位。
  新实现超过 65535 时饱和；旧宏未做上限钳位，两者的越界行为不等价。
  还需对照旧真机检查舍入和标定结果。
- PA8 运行时标称每 100 ms 翻转，由 status 在系统工作队列独立推进，允许调度抖动。
- 默认启用外部看门狗；显式选择 `conf/debug.conf` 才关闭外狗及任务监控，
  PC13 保持 MCUboot 设置的浮空状态。见[故障策略](fault-policy.md)。
- PC13 每 10 ms 翻转；未登记任务没有隐式进展期限；初始化、
  采集、通信初始化等明确故障按策略留驻或停喂复位，普通留驻持续喂狗。原功能表描述外部看门狗在
  200 ms 无边沿时复位；原主循环与 DP 隐式活性检查已解除。
  Modbus 无有效收包约 20 s 时重置端口，恢复 TX 完成中断丢失等状态；
  工作线程持续卡住 20 s 或重配与回退均失败时上报协议维护故障。检测以采样周期为粒度。
  `modules/watchdog` 将停止或 GPIO 错误锁定，任务时限策略由 status 管理；
  `app/status/` 记录采集故障阶段和错误码。停止喂狗不会将 PC13 改为浮空。
- RS485 使用 Zephyr Modbus RTU，115200/8N1，PA15 控制 DE。
  启用非标准串行配置选项以保留旧固件的 8N1；Zephyr 默认无校验时
  使用两个停止位。当前通信地址由协议初始化配置决定，不再采样拨码地址。
  切换期间新请求可能需要重试。串口参数由 IDU 端口配置确定。
- 本地采样在 `app/acquisition/acquisition.c` 中直接调用 Zephyr ADC/GPIO API，
  配置行将硬件 spec 与 device_id/signal_id 绑定，读取后使用 `dp_set_sig_value()` 同步写点。
  16 路 DI 合成为位图，8 路 AI 各自平均、换算并独立写入；不再提交本地整批快照。
  拨码地址暂不采样，地址测点保持点表默认值。每点独立写入，不保证整轮原子性。
- LED 由 `app/status/status_led/status_led.c` 直接调用 Zephyr GPIO API，状态与显示策略归
  `app/status/`；启动和故障常亮，运行时亮/灭各 100 ms，采样周期保持独立。后续按业务需要在 status
  中增加闪烁编码，当前不保留独立 LED 模块或定时闪烁任务。
- DP 内部通过 zbus 消息副本发布采集批次，处理线程执行点表转换与范围检查，
  用互斥锁更新数据池。采集失败时读请求返回异常；当前不启用按样本年龄过期。
- 缺少的第二路串口先使用 Zephyr Modbus RAW client/server 内存回环，
  独立线程按采集表轮询。虚拟点带 SIMULATED 标记，并在 10000 起的
  扩展区转发，不替代原地址的本地硬件数据。
- FC06/FC16 支持本机参数及虚拟下游绝对设定值，整包校验、下发回读、
  保存后才成功应答。独立 NVS 分区保存参数和待恢复事务，未定义的 Flash 区域不参与运行。
  [寄存器与故障处理说明](point-tables.md#参数下发与持久化)。

## 参考

- 本地 `code/APP/Project/Peripheral/Gpio.c`、`include/Gpio.h`。
- 本地 `Drivers/Adc.c`、`Peripheral/AdcDriver.c`、`include/AdcDriver.h`。
- 本地 `Drivers/Sci.c`、`APP/MainTask.c`、`Protocol/ModbusCallBackFun.c`。
- 本地 `Peripheral/W25Qx.c`、`include/W25Qx.h`、`Peripheral/wDog.c`。
- 本地 `code/APP/Firmware/gd32xx_flash_v2.0.ld`。
- [GD32E505 官方数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)。
- 上游 `zephyr/dts/arm/gd/gd32e50x/gd32e50x.dtsi`、
  `zephyr/soc/gd/gd32/gd32e50x/`、`zephyr/samples/subsys/modbus/rtu_server/`。
