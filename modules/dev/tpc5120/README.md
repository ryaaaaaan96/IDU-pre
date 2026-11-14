# TPC5120

3PEAK TPC5120 的 Zephyr 外置 ADC 驱动，不依赖 GD32 寄存器或 APP 测点。
协议依据 [官方手册 BA20241107A1](https://static.3peak.com/res/doc/ds/Datasheet_TPC512x.pdf)
第 11–16 页；检索 Zephyr 官方 ADC Kconfig 和源码清单时未发现 TPC5120 后端。
底层使用标准 SPI API，对调用方提供标准 `adc_channel_setup()` 和 `adc_read()`。

## 支持范围

- 16 路单端输入，12 位无符号原始值，支持单通道及多通道掩码，按通道号升序填充缓冲区。
- 外部参考 `ADC_REF_EXTERNAL0`；`ADC_GAIN_1` 对应 0～VREF，
  `ADC_GAIN_1_2` 对应 0～2×VREF。采集时间只接受 `ADC_ACQ_TIME_DEFAULT`。
- SPI Mode 0、MSB first；两个 8 位字节组成一个 16 时钟 CS 帧。
  `spi-max-frequency` 不得超过 20 MHz；上限不代表板级推荐值。
- 手动模式逐帧写通道和量程，每批读取 N+2 帧，丢弃前两帧，核对结果高四位通道地址。
  每帧结束后等待至少 1 us，再开始下一帧，满足 CS 高电平及安静采样间隔。
- 互斥锁串行化同一芯片的配置和采样；整批成功后才写调用方缓冲区。
  SPI 错误保留原错误码，通道头错误返回 `-EIO`；下一次读取重新建立流水线。
- 同步执行，无专用线程、工作队列或堆分配。禁止从 ISR 调用。

异步读取、序列 options（回调、间隔、额外采样）、过采样、软件校准、差分输入暂不支持，
返回 `-ENOTSUP`。不提供自动扫描模式、芯片 GPIO 或阈值告警配置。
初始化就绪仅代表 SPI 资源可用，芯片没有在此驱动中使用可读身份标识，不能据此判断已接板。

## 接入约定

将本目录登记为 Zephyr 模块，启用 ADC 和 SPI，设备树子节点使用
`compatible = "threepeak,tpc5120"`，启用节点会默认选中 `CONFIG_ADC_TPC5120`。
Zephyr 当前要求 compatible 以字母开头，因此使用 `threepeak` 厂商前缀。
初始化优先级默认 80，必须晚于 SPI 控制器。

板级负责 SPI 引脚、低有效 CS、频率、CS 建立/保持时间、通道量程与外部参考电压配置。
建议 GPIO CS 建立/保持时间各至少 1 us，不使用 hold-CS、半双工或 TI 帧格式。
2×VREF 量程还须满足芯片模拟供电及输入限制。
应用负责采样周期、通道到探头映射、温度换算和异常策略。
原始值的有效性不代表参考电压、传感器接线和前端参数已经确认。
