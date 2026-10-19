# IDU-pre

基于 `/home/ryan/Documents/io-interface-board` 当前启用的
`DEVICE_CONFIG_MV_IO` 分支设计的 Zephyr 4.4.2 应用。
当前已实现本地采集、zbus 点表处理、虚拟 Modbus 采集口和现有 RS485
转发、参数下发和 NVS 持久化。点表配置参考 AIDC 的 equip/process 设计；详见
[点表设计、库接口分析和换串口步骤](docs/point-tables.md)。
对照 MCUdemo 的设备实例与三表设计，通讯点表按采集、处理、转发拆分在
`src/cfg/` 的三个 `.c` 文件，由 `instances.h` 统一绑定设备、总线和转发窗口。
信号、采集和转发映射由点表维护，协议策略归 `src/communication/config/`，
业务执行放在 `src/process/`；新增点和设备见 [通讯点表配置指南](src/cfg/README.md)。
Process 支持无 Flash/NVS 的独立运行，采集线程使用事件与截止时间唤醒。
模块边界、扩展步骤及验证入口见
[collect / process / transmit 模块设计](docs/collect-process-transmit.md)。
GD32 官方固件库、Zephyr 上游及厂商分支的支持范围见
[GD32E505 官方支持索引与适配边界](docs/gd32-support.md)。
当前没有完成整个旧固件或 RT-Thread 平台库的替换；MV-IO 的单 ARM
IAPv3/AES/SGZ 接收已接入 MCUboot，见[兼容升级配置](#iapv3)。
本轮新增事务回调、多输出、独立参数标志、异步/延后保存、多总线采集、
通用转发段与串口动态配置；原业务表作为可选 AIDC 虚拟配置迁入。
[当前能力、构建命令与原表问题](docs/point-tables.md#module-extension)。
2026-10-15 已补 MV-IO 保存地址、247 入口、旧保留区/广播语义、旧 Flash
参数导入及串口恢复，详见[MV-IO 兼容迁移](docs/point-tables.md#mvio-migration)。
已新增独立的 [MCUboot 启动与升级配置](#mcuboot)，原来的构建命令仍生成旧 BOOT
布局的应用。MCUboot 配置已经过交叉编译和桌面检查，尚未在实物验证。
本目录是独立的 IDU-pre 板级应用仓库，包含本板 BSP、应用、点表模块、
MCUboot 配置、打包工具和测试。构建入口 `build.sh` 与应用一起保存。
在本目录执行 `./build.sh`，或在工作区根目录执行 `./applications/IDU-pre/build.sh`。
本机工作区根目录的 `./build.sh IDU-pre idu_pre` 保留为兼容入口。
改动前按旧模块、实际启用配置和 MV-IO 硬件代码逐项核对的缺口见
[旧模块对照清单（2026-10-14）](docs/point-tables.md#legacy-audit)。

## 仓库布局与依赖

```text
IDU-pre/
├── boards/              # idu_pre 板定义、引脚和启动分区 overlay
├── soc/                 # GD32E505 SoC 接入
├── dts/                 # 板级设备树 binding
├── src/
│   ├── main.c           # 应用入口、周期与休眠
│   ├── cfg/             # 点表：信号、设备、三表及采集/处理元数据
│   ├── app/             # 生命周期与 MCUboot 健康确认
│   ├── hardware/        # DI、ADC、LED、外部看门狗
│   ├── equipment/       # 设备服务、点表接入与工程量换算
│   ├── process/         # 测点处理、参数命令和旧参数导入
│   ├── communication/   # Modbus 端口、参数协议、MV-IO 兼容及采集后端
│   └── iap/             # IAPv3 接收、SGZ 与镜像校验
├── include/             # 本板接口
├── modules/
│   ├── dataprocess/      # dp_* 数据池、业务处理及可选 NVS
│   ├── collect/          # co_* 采集与总线调度
│   └── transmit/         # tr_* 地址映射与协议转发
├── conf/                # MCUboot、IAPv3、SMP、AIDC 可选配置
├── sysbuild/            # MCUboot 与应用联合构建
├── tools/               # 镜像检查、签名构建、SGZ 打包和表导入
├── tests/               # 主机与 native_sim 测试
├── docs/                # 迁移设计、能力边界和历史对照记录
└── build.sh             # 本板构建入口，默认板卡 idu_pre
```

工作区继续采用 T3：Zephyr 及 MCUboot/HAL 等上游依赖由独立的
`manifest/west.yml` 固定到 Zephyr `v4.4.2` 的依赖集合；本仓库放在
`<工作区>/applications/IDU-pre/`。先恢复清单并运行 `west update`，
配置 Zephyr Python 环境和 Arm 工具链，再构建本板。
源码、点表与本板 BSP 均在本仓库内，编译不依赖已删除的其他应用或旧工程。
产物和本地签名私钥仍放在工作区 `build/` 下，不进入本仓库。

旧项目只用于可选的对照测试或重新导入点表。重新导入 AIDC 表时显式指定：

```bash
python tools/import_aidc.py --source /path/to/AIDC_CODE/CODE
```

历史审计文档中的旧目录和哈希保留为当时的对照证据，不作为当前构建输入。

## 已运行的硬件基准与移植配置

- 用户确认 `io-interface-board` 已在这套实际硬件运行，它是本次迁移的硬件基准。
  下述上板检查针对 Zephyr 移植，不表示旧硬件方案未经验证。
- 用户确认 MCU 为 GD32E505，外部输入为 **20 MHz 有源时钟**。
- 启动使用 HXTAL bypass，PLL 为 `20 MHz / 2 * 18 = 180 MHz`，
  AHB/APB2 为 180 MHz，APB1 为 90 MHz，ADC 时钟为 APB2 / 6 = 30 MHz。
- Zephyr 4.4.2 没有 GD32E505 SoC 定义。本应用在 `soc/idu/gd32e505/`
  补充 SoC 定义，复用上游 E50x CPU、GPIO、串口和 SPI 支持。
  E505 使用 HAL 的 `GD32E50X_XD`，不再沿用旧工程的 `GD32E50X_CL`。
- 根据旧链接脚本暂按 512 KB Flash、128 KB SRAM 设置。
  **仍需核对 MCU 完整型号后缀对应的容量和封装**。
- 外部 Flash 按旧代码的 4 MB W25Q32、SPI mode 3 设置；驱动用 SFDP
  检测。待验证的是 Zephyr SPI NOR 驱动的上电、JEDEC/SFDP 和写擦行为。
- 代码中的引脚定义是本次迁移依据；参考目录未发现原理图。
  功能说明表中有旧的扩展 DI/DO 介绍，与当前代码不完全一致。

## 文件结构

```text
IDU-pre/
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── VERSION
├── boards/idu/idu_pre/
│   ├── board.yml
│   ├── Kconfig.idu_pre
│   ├── idu_pre.dts
│   ├── idu_pre-pinctrl.dtsi
│   ├── idu_pre_defconfig
│   └── idu_pre.yaml
├── soc/idu/gd32e505/        # E505 SoC 与 20 MHz 有源时钟启动
├── dts/bindings/           # 应用硬件描述绑定
├── conf/collect-rtu.conf   # 补齐硬件后切换真实采集串口
├── include/               # idu_pre.h / idu_equip.h
├── src/
│   ├── main.c              # 入口与采样周期，周期可通过参数修改
│   ├── Kconfig             # 产品选项、采集后端及采样周期
│   ├── cfg/
│   │   ├── collect.c      # 采集字段、读块与周期
│   │   ├── process.c      # 信号、参数与处理绑定
│   │   ├── transmit.c     # 北向寄存器映射
│   │   ├── device_config.c/.h # 设备与三表装配接口
│   │   ├── instances.h    # 设备、总线、地址窗口与周期
│   │   ├── signal_ids.h   # 应用设备和信号 ID
│   │   ├── mvio_model.h   # 共享型号的三表宏
│   │   ├── point_metadata.c/.h # 采集模型和处理绑定描述
│   │   └── generated/    # 导入符号与来源记录
│   ├── app/
│   │   └── boot.c          # MCUboot 试运行健康确认
│   ├── hardware/
│   │   ├── io.c            # DI、拨码地址、运行灯
│   │   ├── analog.c        # 有超时的 ADC 采集与十次平均
│   │   └── health.c        # PC13 外部看门狗
│   ├── equipment/
│   │   ├── equip.c         # 本地样本发布与点表启动
│   │   └── registers.c     # 电流换算
│   ├── process/            # 测点回调、参数命令、MV-IO 导入与 AIDC 处理
│   ├── communication/
│   │   ├── config/        # 协议选择、控制命令与 MV-IO 路由配置
│   │   ├── protocol.c      # RS485 Modbus RTU 端口配置与恢复
│   │   ├── mvio.c          # MV-IO 报文路由与旧协议编码
│   │   ├── parameter_protocol.c # 参数写入、恢复默认、状态诊断
│   │   ├── aidc_serial.c   # AIDC 参数到 Zephyr 串口配置的适配
│   │   └── ports/          # 真实 RTU / 虚拟 RAW 采集后端
│   └── iap/                # IAPv3、SGZ 和镜像校验
├── docs/point-tables.md
└── tests/
    ├── run.sh              # 主机回归 + Zephyr 模拟器测试
    ├── legacy_registers.c  # 早期 Zephyr 映射，仅作为本项目重构回归依据
    ├── test_registers.c
    ├── points/             # 实际 zbus / Modbus 集成测试
    ├── mvio/               # 旧协议、参数迁移、串口故障恢复
    └── test_boot_image.py  # BIN/HEX 启动布局检查器的异常输入测试
```

三个通用模块分别放在 `modules/dataprocess/`、`modules/collect/` 和
`modules/transmit/`，通过 `modules/modules.cmake` 注册到 Zephyr。产品点表留在
`src/cfg/`，模块不依赖本板 BSP；详见 [模块接口](modules/README.md)。
`src/cfg/` 仅保存点表定义及装配所需的设备、信号和生成元数据。
文件平铺，三表直接在各自 `.c` 中维护，不再拆分 `.inc`。导入工具仅更新
标记区域，`generated/` 保留符号和来源记录。
协议策略在 `src/communication/config/`，产品选项在 `src/Kconfig`，
`conf/` 保存可选的 Kconfig 配置组合。目录调整保持既有数据、点序和行为。

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

可选 TCA9535 在设备树中只预留，未启用。旧代码中的 `0x48` 是
8 位写地址，Zephyr 的 7 位地址为 `0x24`。当前 MV-IO 分支没有调用
扩展 DI 读取函数，不能根据旧功能表自动启用。

当前分支的 DO 初始化和输出控制均被注释，部分 DO 引脚与有效 DI
重合，因此本应用不配置继电器输出。旧 NTC 选择引脚 PC1/PC2 也与
当前 ADC 通道重合，本应用仅将其用于模拟输入。

## 已实现行为

- 每 500 ms 获取 16 路 DI 和拨码地址，采集 8 路模拟量。
- 每个模拟通道连续采集 10 次并取整数平均，分辨率 12 bit。
  本版使用逐通道轮询和转换超时，不沿用 FreeRTOS 的连续 DMA 缓冲区。
  平均数和周期由 `CONFIG_IDU_ADC_AVERAGE_COUNT`、
  `CONFIG_IDU_SAMPLE_PERIOD_MS` 调整初始配置；31000 可设置并保存运行周期。
- 保留当前旧代码的电流换算：
  `raw / 4096 * 3.3 / 0.24 * 50 * 100`，线上的整数单位为 0.01 A。
  旧注释中的“4–20 mA”不能直接当作当前返回值的单位。
  新实现超过 65535 时饱和；旧宏未做上限钳位，两者的越界行为不等价。
  还需对照旧真机检查舍入和标定结果。
- PA8 每个采集周期翻转。
- PC13 每 10 ms 翻转；采集主循环超过 1500 ms 没有进展，或初始化、
  采集、通信初始化失败时停止喂狗。原功能表描述外部看门狗在
  200 ms 无边沿时复位。点表处理线程也纳入主循环的进展检查。
  Modbus 无有效收包约 20 s 时重置端口，恢复 TX 完成中断丢失等状态；
  工作线程持续卡住 20 s 或重配与回退均失败时停止喂狗。检测以采样周期为粒度。
- RS485 使用 Zephyr Modbus RTU，115200/8N1，PA15 控制 DE。
  启用非标准串行配置选项以保留旧固件的 8N1；Zephyr 默认无校验时
  使用两个停止位。拨码地址改变后异步重配，等待原响应发送结束后切换。
  切换期间新请求可能需要重试；AIDC 配置也支持原点表中的串口参数。
- 通过 zbus 消息副本发布采集批次，处理线程执行点表转换与范围检查，
  用互斥锁更新数据池。采集失败或数据过期时旧协议读不应答，扩展区返回异常。
- 缺少的第二路串口先使用 Zephyr Modbus RAW client/server 内存回环，
  独立线程按采集表轮询。虚拟点带 SIMULATED 标记，并在 10000 起的
  扩展区转发，不替代原地址的本地硬件数据。
- FC06/FC16 支持本机参数及虚拟下游绝对设定值，整包校验、下发回读、
  保存后才成功应答。独立 NVS 分区保存参数和待恢复事务，保留旧 Flash 区域。
  [寄存器与故障处理说明](docs/point-tables.md#参数下发与持久化)。

## 基础寄存器接口

以下是 **报文中的原始地址**，不额外减 1。

| 功能码 | 地址 | 内容 |
| --- | --- | --- |
| 0x04 | 8149 | 旧设备类型 0x2221 |
| 0x04 | 8150–8151 | 协议号 0x01000101，低字在前 |
| 0x04 | 8152 | 16 路 DI 位图 |
| 0x04 | 8163–8170 | 八路电流值，单位 0.01 A |
| 0x04 | 5486 | 旧功能枚举 0x1204 |
| 0x04 | 2503 | 旧协议长度字段 3 |
| 0x04 | 2504–2513、4989–4998 | 默认 SN 字符串 `SG-FACTORY`，尚非唯一出厂 SN |
| 0x04 | 2568–2571 | 旧协议确认字段 |
| 0x04 | 2581–2595 | 本应用版本字符串 `IDU-pre_0.1.0` |
| 0x03、0x06、0x10 | 30459 | 保存地址，写入 1–246；通信 unit 仍取拨码 1–16 |
| 0x03 | 8149–8158、8178–8187、39278–39285 | 旧保持寄存器保留项，读零；末项从 39284 起读两字 |
| 0x03 | 20003、31996–31997 | 默认 0；`--mcuboot --iapv3` 初始化成功后为 4、0x1200、0 |

默认 MV-IO 使用 `mvio.c` 的旧收包策略。247 只接受起始地址为 30459
的请求；广播 FC04 支持旧读取并应答，广播 FC16 在旧写窗口内执行并应答。
广播 FC03/FC06 和所有扩展参数广播均忽略。旧 30459 越界写 ACK 但不修改；
有效写必须 NVS 保存成功才 ACK，保存失败静默且禁写，状态可读 32000。

读请求按旧有序表跨地址空洞推进；起始地址必须是旧分发门控允许的完整
表项。8151、39285 不能单独作为起始点，2581 必须单独读取 15 字。
4805–4814 仍保留内部别名，其中 4814 已改为保存地址，但与旧代码一致，
其直接请求入口关闭。旧窗口内未知写表项只 ACK；窗口外请求静默。
扩展地址 31000、31001–31008、31500–31502 等保留标准异常响应。
精确窗口、已知差异和测试入口见[兼容说明](docs/point-tables.md#mvio-migration)。

`--mcuboot --iapv3` 增加旧 IAPv3 收包、AES/SGZ 解包和签名镜像写入。
协议与旧代码对照及桌面测试已完成，旧上位机实测仍待完成。
AIDC 可选配置不启用这些 MV-IO 特殊收包规则。

新增远程点表地址、质量标记、虚拟数据和第二路串口替换方法见
[点表说明](docs/point-tables.md#idu-pre-配置和寄存器)。

## Flash 与启动布局

内部 Flash：

| 区域 | 相对 Flash 起始偏移 | 大小 |
| --- | --- | --- |
| 旧启动程序 | 0x0000 | 16 KB |
| Zephyr 应用 | 0x4000 | 496 KB |

应用 HEX 链接到 `0x08004000`，BIN 对应的烧录地址也是
`0x08004000`。BIN 不自带地址。本工程不生成旧 BOOT + APP 合并固件。
旧链接脚本从 `0x08004000` 起仍声明 512 KB，这是越过芯片总容量的
布局；新分区从总计 512 KB 中扣除了启动区。

外部 4 MB SPI Flash 保留原偏移：

| 区域 | 偏移 | 大小 |
| --- | --- | --- |
| 参数主区 | 0x000000 | 4 KB |
| 参数备份 | 0x001000 | 4 KB |
| Zephyr NVS 参数/事务 | 0x010000 | 16 KB（4 × 4 KB） |
| 可选 AIDC NVS 参数/事务 | 0x014000 | 16 KB（4 × 4 KB） |
| 升级配置区 | 0x200000 | 1 MB，预留范围 |
| 升级镜像区 | 0x300000 | 1 MB |

旧参数和升级分区标为只读。参数模块只把独立 NVS 分区交给存储后端，
不写旧区，也不直接保存 C 结构体。SPI NOR 页布局按 4 KB 扇区配置。

后续功能迁移设计：

1. 旧参数导入已实现：首次空 NVS 或已知上一版参数格式升级时，只读
   校验主/备区的长度和 Modbus CRC16，解析 8 字节 `G_FLASH_T` 保存地址。
   新 NVS 成功提交后不再导入；已有其他参数和待恢复事务保留。
   CRC 正确但长度未知时拒写并保留现场。仍需实际 Flash 样本/掉电验证。
2. IAP 迁移已增加独立配置：保留 SGZ 接收偏移，解包后写入 MCUboot
   slot1，并校验 CRC、向量、SHA256、密钥哈希及 ECDSA 签名。
3. MCUboot 首次安装需要更新启动程序和应用；旧 BOOT 的 16 KiB 分区
   不适用于新的 64 KiB 启动程序。旧裸 BIN 需要重新构建和签名。

## 构建与验证

从工作区根目录执行：

```bash
./applications/IDU-pre/build.sh
```

本次使用本机已有的 GNU Arm 工具链进行了编译：

```bash
env ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
    GNUARMEMB_TOOLCHAIN_PATH=/home/ryan/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3 \
    ./applications/IDU-pre/build.sh
```

`CONFIG_KERNEL_BIN_NAME="IDU-pre"` 设置固件名称。
输出在 `build/IDU-pre/idu_pre/zephyr/`，包含 `IDU-pre.hex`、
`IDU-pre.bin` 和 `IDU-pre.elf`。构建脚本在成功后自动复制 HEX 到
`build/hex/IDU-pre.hex`，覆盖该项目上次导出的 HEX；不同板卡使用
同一项目名时，这里保留最后一次成功导出的版本。原构建目录保留全部
产物。构建失败时不会执行复制。本次没有配置具体芯片后缀对应的烧录
runner，也没有执行烧录。导出前自动执行 `tools/check_boot_image.py`，
检查应用边界、SP/Thumb 复位向量及 HEX/BIN 一致性，报告为构建目录的
`IDU-pre-boot-check.json`。这是静态检查，不能代替旧 BOOT 真机跳转验证。

主机侧寄存器/换算和 Zephyr zbus/Modbus 集成测试：

```bash
./applications/IDU-pre/tests/run.sh
```

已进行设备树/Kconfig 生成、完整链接、主机寄存器边界测试和
Zephyr 原生模拟器集成测试，含参数持久化、写失败注入和三进程重启
恢复验证。`legacy_registers.c` 没有运行旧 MV-IO 协议实现，不能作为
新旧固件等价的证明；虚拟 RAW 模式不模拟物理串口时序。
上板仍需验证：20 MHz 输入及 PLL/总线时钟、启动程序跳转、PC13
喂狗间隔、16 路 DI 顺序、拨码地址、八路采集标定、DE 最后一个停止位
结束后释放、SPI NOR 上电/写擦/掉电恢复。编译通过不代表这些硬件测试已完成。

## MCUboot

MCUboot 可替代旧 BOOT 的启动校验、镜像安装和升级状态管理。本配置选择
ECDSA P-256 签名、`swap-scratch` 交换、试运行后确认；未确认的试运行镜像
在下次复位时回退。MCUboot 是独立启动程序，不是链接进主应用的一个任务。
官方说明：[Zephyr + MCUboot](https://docs.mcuboot.com/readme-zephyr.html)、
[升级与回退设计](https://docs.mcuboot.com/design.html)、
[Zephyr MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)。

标准 SMP 和[旧 IAPv3](#iapv3) 使用独立的应用配置，共用同一 MCUboot
签名和交换布局。SMP 接收签名 BIN；IAPv3 接收封装该 BIN 的 AES/SGZ 包。
包内必须是针对新地址构建并签名的 MCUboot 镜像。旧 `IAPV3_INFO` 不再作为新
启动程序的升级命令；旧参数导入和 NVS 仍按原流程工作。

内部 Flash 和外部 NOR 的地址空间分别计算：

| 位置 | 地址或偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| 内部 Flash | `0x08000000` | 64 KiB | MCUboot |
| 内部 Flash | `0x08010000` | 448 KiB | slot0，包含 512 字节镜像头及尾部交换状态 |
| 外部 W25Q32 | `0x300000` | 448 KiB | slot1，接收新镜像/保存回退镜像 |
| 外部 W25Q32 | `0x370000` | 8 KiB | scratch，交换暂存 |

外部参数区和原 NVS 保留。IAP 配置会使用原 SGZ 暂存区，详见下节。
内部擦除页依据 GD32E50x 用户手册
为 8 KiB，写粒度设置为 4 字节；外部驱动使用 4 KiB 页布局。导出工具保守预留
完整 8 KiB 尾部，不接受超过 440 KiB 的签名升级文件。
MCUboot 在 SoC 早期初始化中沿用旧 `wdog_close()`，将 PC13 设为浮空输入；
应用 `idu_health_init()` 恢复 GPIO 输出和喂狗。实物上仍需验证这一交接及擦写时序。

保留当前工具链设置，在工作区根目录执行：

```bash
# MCUboot + MV-IO 应用，独立目录输出
./applications/IDU-pre/build.sh --mcuboot

# 增加标准 SMP 升级入口：USART0，115200 8N1；USART2 继续转发 Modbus
./applications/IDU-pre/build.sh --mcuboot --smp-uart

# 可选 AIDC 业务表 + 标准升级入口
./applications/IDU-pre/build.sh --mcuboot --aidc --smp-uart

# 后续版本：使用同一签名密钥，更新镜像版本
./applications/IDU-pre/build.sh --mcuboot --smp-uart --version 0.1.1+1
```

脚本首次生成本地开发密钥 `build/keys/IDU-pre-dev.pem`（权限 600），后续复用；
不是 MCUboot 仓库公开的示例密钥。清理整个 `build/` 会丢失此密钥，应另行备份；
正式设备使用 `--key /受控目录/签名密钥.pem`。固件和导出目录只包含公钥，
已经安装的 MCUboot 只能接受其公钥对应的签名。

标准 SMP 配置输出在 `build/hex/IDU-pre-mcuboot-smp/`：

| 文件 | 用途 |
| --- | --- |
| `IDU-pre-factory.hex` | MCUboot 与签名应用合并，用于首次安装新布局 |
| `IDU-pre-boot.hex` | MCUboot 单独产物 |
| `IDU-pre.hex` | 签名应用 HEX，要求设备已有匹配的 MCUboot |
| `IDU-pre-update.bin` | 后续通过 SMP 上传的签名镜像 |
| `IDU-pre-public.pem` | 对应公钥 |
| `IDU-pre-validation.json` | 签名、向量、边界检查及产物摘要；明确记录未做硬件验证 |

不加 `--smp-uart` 的输出目录为 `build/hex/IDU-pre-mcuboot/`；AIDC + SMP 为
`build/hex/IDU-pre-mcuboot-aidc-smp/`。原 `build/hex/IDU-pre.hex` 始终属于旧 BOOT
布局。HEX 与升级 BIN 从同一次签名结果生成，避免两次 ECDSA 签名造成内容差异。

首次切换需要通过调试器安装新 BOOT 和新应用，**不能仅把新应用发给旧 BOOT**。
首次安装还应清空外部 `0x300000..0x371fff` 的新 slot1/scratch 区，保留参数和
NVS；普通后续升级由 MCUboot 管理交换区，不能擅自清空仍保存回退镜像的 slot1。
当前尚未配置实物烧录 runner，也没有执行擦除或烧录。

SMP 客户端执行“上传 `IDU-pre-update.bin` → 查询镜像 → 把新镜像设为 test →
复位”。新应用连续 10 秒完成采集、转发、健康检查，且参数存储就绪、没有待恢复
事务后自动确认；60 秒仍未满足条件则停止喂狗，由复位触发回退。此确认只证明
当前健康检查通过，不涵盖现场所有业务工况。客户端应采用 test 流程，避免人工
提前确认绕过应用自检。升级/复位前应等待正在进行的参数操作和延后保存完成。

桌面验证包括原有回归测试、试运行确认策略测试、固件交叉编译、官方 imgtool
验签以及签名破坏/错误密钥的拒绝检查。实际换版、掉电中断交换、回退、SPI NOR
时序和 watchdog 交接仍须上板验证。

```bash
source .venv/bin/activate
python applications/IDU-pre/tests/test_mcuboot_artifacts.py \
    build/hex/IDU-pre-mcuboot-smp
```

当前 MCUboot 占用 Flash 36,632 B / RAM 18,096 B；MV-IO + SMP 应用占用
Flash 96,416 B / RAM 81,192 B。AIDC + SMP 应用 RAM 为 120,848 B（92.2%），
后续增加兼容升级解包缓冲区时需要重新核算。BOOT 与应用分时运行，RAM 占用不相加。

<a id="iapv3"></a>
## 旧 IAPv3 / AES / SGZ 兼容升级

适用当前 MV-IO 单 ARM 硬件。现有 USART2/RS485 保持转发用途，升级地址段
在同一端口内路由；虚拟采集口继续工作。此配置与 `--aidc`、`--smp-uart`
互斥：AIDC 的升级入口/多设备升级尚未移植，也不允许两个镜像上传端同时写 slot1。

```bash
./applications/IDU-pre/build.sh --mcuboot --iapv3
# 后续版本沿用同一签名密钥：
./applications/IDU-pre/build.sh --mcuboot --iapv3 --version 0.1.1+1
```

产物位于 `build/hex/IDU-pre-mcuboot-iapv3/`：

| 文件 | 用途 |
| --- | --- |
| `IDU-pre-factory.hex` | 首次通过 SWD 安装 MCUboot + 应用 |
| `IDU-pre-boot.hex` / `IDU-pre.hex` | 分开的启动程序 / 签名应用 HEX |
| `IDU-pre-update.bin` | 签名 MCUboot 镜像 |
| `IDU-pre-update.sgz` | 包含上述镜像的 SGZ；除前 16 字节外已做旧 AES 加密 |
| `IDU-pre-public.pem` / `IDU-pre-validation.json` | 公钥及产物校验记录 |

SGZ 生成器先用官方 imgtool 验签，然后封装单子文件、type=2、2048 字节块。
它输出 QuickLZ 的合法 stored 帧；接收端同时支持旧工具生成的实际压缩帧
（QuickLZ 1.5、level 3、无跨块历史）。没有把旧 FreeRTOS 分配器或压缩器链接进应用。
当前旧上位机软件未提供，尚不能判断它读取文件后是否再次执行 AES：默认 SGZ
适合直接分包发送；如果工具自己加密，使用 `pack_sgz.py --plain` 生成明文包，避免双重加密。

```bash
python applications/IDU-pre/tools/pack_sgz.py \
  --bin build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update.bin \
  --key build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-public.pem \
  --output build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update-plain.sgz --plain
```

协议保留的主要入口（地址均为线上原值，不减 40001）：

| 地址 | 行为 |
| --- | --- |
| 20049 / 20051 | 通信/协议版本 0x03000000；32 位数低字在前，每字大端 |
| 20055 / 20057 / 20059 | 首包延迟 3000 ms、分包间隔、状态超时 480000 ms |
| 20061 / 20062 | 下级设备数 0、属性 3 |
| 20063–20070 / 20071–20078 | 16 字节状态 / 原因；byte 15 为总状态，byte 0 为 ARM 状态 |
| 20083 | 0xAA 登录；0x55 退出；支持登录、保留字、包参数的合并写入 |
| 20088 / 20089 | 每包长度 / 总包数；长度 16–1024，16 字节对齐 |
| 20090–20094 | 包号从 1 开始、旧地址字段、本包长度、数据 |
| 20099–20103 | 首个缺包号、偏移、长度、缺包总数；读取总数为 0 时启动解包 |

FC03/04 读取、FC06/16 写入；FC16 按完整寄存器数判断长度，兼容旧 1 KiB
非标准长帧的 8 位 byte-count 字段。仅 IAP 地址段接受这种帧，普通 Modbus
边界仍由原路由校验。IAP 广播 FC16 执行但静默，广播 FC04 保留旧应答，247
不进入 IAP。未知起点、截断帧、非法包号/长度等静默拒绝，整帧验证后才改变会话。
旧包地址字段仅保存/回读；写入位置与旧实现一致，由 `(包号 - 1) × 每包长度` 确定。
20093 仅决定最后一包的长度；非末包使用 20088 的每包长度，兼容该字段填 0 的发送端。

单包 ACK 表示已进入异步处理；只有解密、Flash 写入及回读比对成功后才清除
缺包标记。队列忙时静默，主机应按间隔发送并查询/补发缺包。同一包重发必须
内容一致；乱序包和跨扇区包均支持。Flash 按需擦除，解包线程独立于转发线程。

IAP 配置额外划分两个外部 NOR 区域：`0x18000–0x1BFFF` 为独立 NVS
升级日志，`0x201000–0x2F0FFF` 为 960 KiB SGZ 暂存。旧升级配置
`0x200000–0x200FFF` 只读；slot1 位于 `0x300000`，不会从应用直接写当前镜像或 BOOT。
日志使用固定 48 字节小端布局和自身 CRC；不启用会改变原参数 NVS 格式的全局数据 CRC。

解包校验 SGZ CRC、子文件 CRC、输出长度与 CRC，然后校验 MCUboot 头、
本板向量、SHA256、签名密钥哈希和 ECDSA-P256。公钥从本次 MCUboot 构建的
同一个签名密钥生成；AES 只用于旧传输兼容。全部通过后，原子保存并暂时冻结
参数写入，提交升级日志，调用 `boot_request_upgrade(BOOT_UPGRADE_TEST)`，
等待 500 ms 后复位。原健康策略连续运行 10 s 才确认试运行镜像。

掉电中断收包不会续接未知数据，状态报失败，需要重新登录。升级后读取当前
镜像哈希，且等待健康确认后才报告成功；回退到旧镜像报告失败。压缩差分包、
多个子文件、PLC/Eye/DSP 对下升级不支持；它们不会触发镜像切换。旧裸 BIN
及旧链接地址的固件也不能直接用于此配置。

验证入口：

```bash
bash applications/IDU-pre/tests/iap/run.sh
# 或运行包含原有点表、参数、MV-IO、启动策略的全部回归：
bash applications/IDU-pre/tests/run.sh
```

默认测试无需旧工程；依赖旧 QuickLZ 压缩器的两项互操作测试会明确显示为
跳过。若要运行这些额外测试，设置参考目录后执行：

```bash
IDU_LEGACY_IAP_DIR=/path/to/io-interface-board/code/APP/Project/Iapv3 \
    bash applications/IDU-pre/tests/iap/run.sh
```

已通过旧压缩器生成数据的互解、真实签名镜像 SGZ 还原、3 万组畸形输入的
ASan/UBSan 检查、native_sim 接收/Flash/签名拒绝测试和 7 个独立进程重启阶段。
模拟复位测试验证日志与状态，未执行实际 MCUboot 交换。旧上位机长帧时序、
W25Q32 擦写掉电、真实交换/回退及 watchdog 交接仍待实板验证。
当前 IAP 应用占用 Flash 103,492 B / RAM 108,968 B（83.14%）；
升级线程栈、最大缺包位图和解包缓冲区均已计入。

## 参考

- 本地 `code/APP/Project/Peripheral/Gpio.c`、`include/Gpio.h`。
- 本地 `Drivers/Adc.c`、`Peripheral/AdcDriver.c`、`include/AdcDriver.h`。
- 本地 `Drivers/Sci.c`、`APP/MainTask.c`、`Protocol/ModbusCallBackFun.c`。
- 本地 `Peripheral/W25Qx.c`、`include/W25Qx.h`、`Peripheral/wDog.c`。
- 本地 `code/APP/Firmware/gd32xx_flash_v2.0.ld`。
- [GD32E505 官方数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)。
- 上游 `zephyr/dts/arm/gd/gd32e50x/gd32e50x.dtsi`、
  `zephyr/soc/gd/gd32/gd32e50x/`、`zephyr/samples/subsys/modbus/rtu_server/`。
