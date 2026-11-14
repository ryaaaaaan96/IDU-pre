# 硬件与本地采样

[文档导航](README.md) · [GD32E505 模块](../modules/platform/gd32e505/README.md)

## 平台配置

GD32E505VET6，内部 Flash 512 KiB、SRAM 128 KiB。20 MHz 有源输入采用 HXTAL bypass，
PLL /5 ×45；CPU/AHB/APB2 为 180 MHz，APB1 为 90 MHz，ADC 时钟为 30 MHz。
本板的 SoC、时钟及 ADC 由自有 gd32e505 模块接入，复用上游 HAL 和公共驱动。

HAL 使用 GD32E50X_CL，PLLMF[4] 位于 bit29。MCUboot 和应用共用硬件配置；
板级早期钩子释放 JTAG、保留 SWD，使 PA15 可用于 RS485 DE。
MCUboot 将 PC13 设为浮空，应用 status_init 再启动外部喂狗。

## 引脚

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


DI 低电平有效，DI1 对应位图 bit0，DI16 对应 bit15。拨码引脚保留硬件定义，当前不采样。
DO 与部分 DI 引脚重合，当前不配置输出；PC1/PC2 用于 ADC，不用于旧 NTC 选择。

## 采样与通信

- 主循环按 DP 采样周期读取 DI 和 8 路 AI，初始 500 ms、允许 100–1000 ms。
- ADC 配置使用具名通道，每路 10 次整数平均，12 位、3.3 V、71.5 周期。
- 每路 AI 的 spec 与 device_id/signal_key 在一行配置；DI 合成为一个位图写点。
- 电流采用 `raw × 68750 / 4096`，单位 0.01 A，超过 UINT16_MAX 饱和。
- ADC/GPIO 直接调用 Zephyr API，测点同步写 DP；整轮采样不保证原子性。
- RS485-1 为 USART2、115200 8N1，方向控制与接收队列由 APP 管理；
  transmit 调用 RAW 服务端，不启用官方 Modbus SERIAL 后端。
- USART0 用于 Shell 和日志，115200 8N1；命令见[项目入口](../README.md#串口调试命令)。
- 外部 Flash 按 W25Q32 / SPI mode 3 描述，由驱动读取 JEDEC/SFDP；实际装配容量须实板确认。

参数与镜像区域见[构建与升级](build-and-upgrade.md)，LED 与看门狗策略见[故障策略](fault-policy.md)。

## 上板检查

检查启动日志、115200 串口位宽约 8.68 µs、ADC 标定和采样周期；
验证 PC13 交接、DE 时序、SPI NOR 读写擦除、完整上传和掉电恢复。
这些行为不能由模拟器或编译成功推定，当前验收范围见[验证说明](testing.md)。
