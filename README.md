# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.4.2 应用，提供本地采样、通讯点表、
Modbus 采集与转发、NVS 参数持久化及可选 MCUboot 升级。
通用能力由 `dataprocess`、`collect`、`transmit` 和 `quicklz` 四个模块提供。
默认使用虚拟南向采集口，现有 USART2 / RS485 用于北向转发。

## 快速开始

本仓库位于 West 工作区的 `applications/IDU-pre/`；Zephyr、MCUboot 和 HAL
由工作区 `manifest/west.yml` 固定。配置 Python 环境和 Arm 工具链后，
从工作区根目录执行：

```bash
# 默认固件：旧 BOOT 布局
./applications/IDU-pre/build.sh

# MCUboot + 旧 IAPv3 / AES / SGZ 升级
./applications/IDU-pre/build.sh --mcuboot --iapv3

# 主机回归与 Zephyr native_sim 测试
bash applications/IDU-pre/tests/run.sh
```

首次恢复工作区或有意同步依赖时运行 `west update`。构建/测试脚本在未激活
虚拟环境时自动使用工作区 `.venv`。也可在本仓库中执行 `./build.sh`。
默认 HEX 导出为工作区 `build/hex/IDU-pre.hex`；MCUboot 产物使用独立目录。
配置选项、签名密钥、分区和首次安装步骤见[构建与升级](docs/build-and-upgrade.md)。

## 阅读文档

完整导航见 [docs/README.md](docs/README.md)。

| 要做的事 | 文档 |
| --- | --- |
| 了解模块职责、数据流与初始化顺序 | [架构设计](docs/collect-process-transmit.md) |
| 新增设备、测点或导入原表 | [点表维护指南](app/cfg/README.md) |
| 查询寄存器、质量和参数事务规则 | [点表与寄存器](docs/point-tables.md) |
| 使用通用模块/API | [模块接口](modules/README.md)、[DP 使用说明](modules/dataprocess/README.md) |
| 迁移参考平台调用 | [接口覆盖、差异与 Flash/RAM 分工](docs/reference-platform.md) |
| 核对 MV-IO 旧报文行为 | [MV-IO 兼容规则](docs/mvio-compatibility.md) |
| 构建、签名、使用 SMP 或 IAPv3 | [构建与升级](docs/build-and-upgrade.md) |
| 核对引脚、时钟与移植条件 | [硬件说明](docs/hardware.md)、[GD32 支持调研](docs/gd32-support.md) |
| 运行测试、查看已记录结果 | [验证说明](docs/testing.md) |

## 应用架构与模块边界

应用入口按三层组织，硬件访问和业务配置通过服务接口连接：

```text
app/main.c                         启动入口、采样周期与休眠调度
  └─ runtime.c                     初始化顺序、单次采集流程、故障停机与升级确认
       ├─ hardware/                DI、ADC、LED 与外部看门狗
       ├─ startup.c                点表管线初始化装配
       ├─ point_service.c          本地采样映射与设备健康状态
       └─ communication/protocol.c Modbus 端口配置与恢复
```

`app/idu_runtime.h` 定义运行生命周期：零初始化后启动，正常状态才能执行
采集周期，失败后停止喂狗并拒绝继续运行。`app/idu_equipment.h` 提供应用级
设备服务；运行层不直接依赖点表、NVS 或 Flash 设备树细节。
芯片专用的 JTAG/SWD 配置归属 `app/hardware/io.c`。点表配置、处理、参数事务和协议兼容
继续分别位于 `app/cfg/`、`modules/` 下的三类通用模块和 `app/communication/mvio.c`。

新增功能时，硬件操作放入对应板级模块，设备数据与存储策略放入设备服务，
运行顺序和故障策略由 `app/runtime.c` 编排。`runtime` 不负责休眠，也不动态分配内存。
`app/CMakeLists.txt` 汇总功能目录，各目录的 CMakeLists 负责本组源码及可选配置；
应用头文件与所属功能源码同目录，通用模块继续独立放在 `modules/`。
`tests/runtime/` 使用模拟服务覆盖启动失败、无效采样发布、处理超时、通信故障
和升级确认失败；完整回归入口 `tests/run.sh` 已包含这些用例。

## 仓库布局

```text
IDU-pre/
├── boards/、dts/        # IDU 板定义、早期板级初始化及设备树
├── app/
│   ├── main.c          # 应用入口、采样周期与休眠
│   ├── startup.c       # DP、采集总线、参数恢复与处理启动
│   ├── point_service.c # 本地样本到信号的映射
│   ├── runtime.c       # 启动、采样编排与故障策略
│   ├── boot.c          # MCUboot 试运行健康确认
│   ├── hardware/        # GPIO、ADC、LED、外部看门狗
│   ├── cfg/             # devices/ 设备与型号；tables/ 三表及元数据
│   ├── process/         # 工程换算、业务回调、参数命令和旧参数导入
│   ├── communication/   # 端口、协议、路由与采集后端
│   └── iap/             # 产品选项和旧 IAP 接口名称兼容头，无独立实现
├── modules/             # gd32e505、dataprocess、collect、transmit、quicklz、iap
├── bootloader/          # MCUboot 配置、分区和联合构建
├── sysbuild/            # Zephyr 约定的薄入口
├── conf/                # 应用可选配置
├── tools/、tests/       # 导入、镜像工具及回归测试
├── docs/                # 按主题组织的说明与历史记录
└── build.sh             # 构建入口
```

运行和构建不依赖旧工程或预编译 `plateform.lib`。重新导入和可选互操作测试
才需要显式提供参考源码。产物在工作区 `build/`，开发签名密钥在
`build/keys/`，不进入仓库；清理前备份需要保留的密钥。

## 头文件与启动程序边界

应用以 `app/` 为头文件搜索根。共用服务头在 `app/`，硬件、处理和通信头
分别与实现放在 `app/hardware/`、`app/process/`、`app/communication/`；
跨功能引用使用 `"hardware/idu_pre.h"` 等路径；通用 IAP 使用 `<iap/iap.h>`、`<iap/sgz.h>`。
通用模块继续保留各自的公开 `include/`，由 Zephyr 模块机制导出。

[`bootloader/`](bootloader/README.md) 保存本板 MCUboot 配置、分区和联合构建设置。
MCUboot 源码仍使用 West 工作区中的独立仓库；`app/boot.c` 负责应用健康确认。
[`modules/iap/`](modules/iap/README.md) 负责升级接收、解包和验签，分区／内存取自
设备树，密钥来源、镜像上限和保存策略由 `conf/iapv3.conf` 等 Kconfig 配置提供，
无需产品适配 `.c`。根 `sysbuild/` 仅保留 Zephyr 约定入口。

[`modules/gd32e505/`](modules/gd32e505/README.md) 通过 Zephyr 的 `soc_root` / `dts_root` 提供
E505 登记、设备树时钟配置和标准 ADC 驱动；芯片启动由主应用和 MCUboot 共用，
ADC 按配置启用。根目录不再单独设置 `soc/`。
IDU 的引脚、外部看门狗和分区属于板级及产品配置，分别留在 `boards/` 和 `bootloader/`。

## 实现与验证范围

点表核心使用 Zephyr 的线程、互斥锁、zbus、消息队列、Modbus 和 NVS，
不增加 ARC 封装。同步 `dp_set_sig_value()` 的长度单位为字节；异步遥测使用
`dp_submit_sig_value(s)`，长度单位为 16 位 word，成功表示复制入队。

参考平台四个公开头文件的 89 个接口名称中，当前覆盖 79 个；
[接口说明](docs/reference-platform.md) 列出限制和其余 10 个入口的原因。
可选 AIDC 配置已迁入点表与部分业务，DSP 仍使用虚拟模型，UPP 完整协议未实现。

已记录的桌面验证包括主机回归、native_sim、固件构建和镜像检查。
Zephyr 版的实际启动、UART/DE 时序、Flash 掉电和升级回退仍需实板验证。
历史测试数量、容量和外部支持状态均以文档中的日期或提交为准。

## 原 README 入口

<a id="已运行的硬件基准与移植配置"></a>
<a id="引脚映射"></a>
<a id="已实现行为"></a>
[硬件基准、引脚与板级行为](docs/hardware.md) 已移至硬件文档。

<a id="基础寄存器接口"></a>
[基础寄存器接口](docs/mvio-compatibility.md#基础寄存器接口) 已移至 MV-IO 文档。

<a id="flash-与启动布局"></a>
<a id="构建与验证"></a>
<a id="mcuboot"></a>
<a id="iapv3"></a>
[Flash 布局](docs/build-and-upgrade.md#flash-与启动布局)、
[构建](docs/build-and-upgrade.md#默认固件构建)、[MCUboot](docs/build-and-upgrade.md#mcuboot)、
[IAPv3 / AES / SGZ](docs/build-and-upgrade.md#iapv3) 和[验证](docs/testing.md) 已按主题归档。
