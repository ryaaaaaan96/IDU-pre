# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.4.2 应用，提供本地采样、通讯点表、
Modbus 采集与转发、NVS 参数持久化及 MCUboot 升级。
点表链路由 `dataprocess`、`collect`、`transmit` 提供，其他通用能力见[模块列表](modules/README.md)。
当前按同线性能联调配置：北向转发和南向周期采集共用 USART2 / RS485。
内置模拟从机已移到 `tests/fixtures/`，产品固件不再编译模拟设备。
南向站号为 1、2，北向在 `.conf` 以 `CONFIG_IDU_FORWARD_UNIT=16` 初始化固定；同线冲突在启动时拒绝。
拨码输入仍是本地测点；北向普通读读取 DP 缓存。
[`vport`](modules/vport/README.md) 用于两个逻辑口的完整报文收发；
CO/TR 各自的[RAW 适配](docs/communication-ports.md)通过通用端口收发完整报文，
实际串口继续复用 Zephyr 的 RTU 组帧和 DE 控制。绑定、收发编排回调和注册入口集中在 `app/protocol/ports.c`。

## 快速开始

本仓库位于 West 工作区的 `applications/IDU-pre/`；Zephyr、MCUboot 和 HAL
由工作区 `manifest/west.yml` 固定。配置 Python 环境和 Arm 工具链后，
从工作区根目录执行：

```bash
# 当前工作固件：MCUboot + IAPv3 / AES / SGZ 升级
./applications/IDU-pre/scripts/build.sh --mcuboot --iapv3

# 主机回归与 Zephyr native_sim 测试
bash applications/IDU-pre/tests/run.sh
```

首次恢复工作区或有意同步依赖时运行 `west update`。构建/测试脚本在未激活
虚拟环境时自动使用工作区 `.venv`。也可在本仓库中执行
`./scripts/build.sh --mcuboot --iapv3`，产物位于 `build/hex/IDU-pre-mcuboot-iapv3/`。
`./scripts/build.sh` 默认构建同一套 MCUboot + IAPv3 固件；旧 BOOT 构建入口已删除。
配置选项、签名密钥、分区和首次安装步骤见[构建与升级](docs/build-and-upgrade.md)。
Linux 独立烧录使用 `./scripts/flash.sh --host <Windows主机IPv4>`，连接已有 J-Link GDB Server，完整擦写内部 Flash 并安装 factory HEX，成功后复位运行并退出；可先加 `--dry-run` 预览。
J-Link GDB Server 界面可直接加载 `scripts/gd32e505vet6.JLinkScript`；Linux 端使用 `scripts/debug.sh`（默认下载签名 HEX、使用 ELF 符号，`--attach` 只连接），Windows 自动启动入口保留为 `scripts/debug-server.py`。设置及线程调试见 [WSL/J-Link 方案](docs/wsl-jlink.md)。

## 阅读文档

完整导航见 [docs/README.md](docs/README.md)。

| 要做的事 | 文档 |
| --- | --- |
| 了解模块职责、数据流与初始化顺序 | [架构设计](docs/collect-process-transmit.md) |
| 新增设备、测点或导入原表 | [点表维护指南](app/protocol/README.md) |
| 查询寄存器、质量和参数事务规则 | [点表与寄存器](docs/point-tables.md) |
| 调整函数、文件和硬件配置 | [命名与采样边界](docs/naming.md) |
| 使用通用模块/API | [模块接口](modules/README.md)、[DP 使用说明](modules/dataprocess/README.md) |
| 迁移参考平台调用 | [接口覆盖、差异与 Flash/RAM 分工](docs/reference-platform.md) |
| 构建、签名、使用 SMP 或 IAPv3 | [构建与升级](docs/build-and-upgrade.md) |
| 核对引脚、时钟与移植条件 | [硬件说明](docs/hardware.md)、[GD32 支持调研](docs/gd32-support.md) |
| 运行测试、查看已记录结果 | [验证说明](docs/testing.md) |

## 应用架构与模块边界

应用按启动、周期执行、测点业务和状态管理组织：

```text
app/main.c                         启动顺序、采样周期与休眠
  ├─ protocol/                     产品设备、点表及模块配置
  ├─ modules/                      点表、采集、转发、输入和升级服务
  └─ acquisition/acquisition.c     每周期采样、发布、转发和健康检查
       ├─ process/points.c         原始样本映射和公共业务回调
       └─ status/status.c          业务阶段、故障、LED 和试运行确认条件
```

`main.c` 中的 `startup()` 显式组织状态与输入初始化、点表注册、端口和总线
建立、参数恢复、INIT 和采集启动。启动成功后按周期调用 `acquisition_cycle()`；
不再设置独立的 startup 文件或设备服务兼容入口。
`app/status/status.h` 定义业务阶段，启动或采集失败时保留最初故障、停止喂狗，
并拒绝继续执行。MCUboot 确认条件由 `status` 检查 Flash 和参数事务状态，
持续健康窗口由 `status/image_confirm/image_confirm.c` 判断。
多任务看门狗由 status 显式登记管理；无活动登记项时持续喂狗，现有任务暂未接入，见[任务看门狗](docs/task-watchdog.md)。

新增功能时，通用机制放进对应模块，产品数据放进 `protocol/`，测点业务放进 `process/`。
启动依赖顺序由 `main.c` 管理，每周期工作由 `acquisition` 编排；不在这些文件中
重新实现协议、存储或驱动。芯片专用 JTAG/SWD 配置仍归板级早期初始化。
`app/CMakeLists.txt` 汇总功能目录，各目录管理本组源码；公共头文件与实现同目录。
`dataprocess`、`collect`、`transmit` 未启用时不创建空库，
公开头文件仍可供独立测试和调用方使用。
完整 IDU 应用会在 CMake 配置阶段检查所需模块；关闭必要服务时直接报告缺失选项，
不用等到链接失败。当前工作配置启用 IAPv3，在 `prj.conf` 手动设置一个采集总线槽、
一个独立转发口及各自栈大小；固件类型不自动覆盖容量，
后续扩展时手动修改，见[构建与升级](docs/build-and-upgrade.md#配置选择)。

`tests/acquisition/` 使用真实的 `main.c` 启动流程和状态机，替换模块实现以验证
初始化顺序、失败短路、存储降级、采样和通信故障；`tests/adc/`、`tests/io/`
分别验证统一采样、GPIO 和看门狗。均已接入 `tests/run.sh`。

引脚、极性、ADC 通道顺序、DI/拨码位序、地址偏移和喂狗时序由板级 DTS 配置，
平均次数由 `CONFIG_IDU_ADC_AVERAGE_COUNT` 配置。业务层统一通过
`acquisition_inputs_sample()` 获取快照；`acquisition/config.c` 将设备树配置交给通用 `input_sampler`，模块不认识点号，也不自动写 DP。
采样字段到测点的映射由 `process/points.c` 完成，点表定义仍在 `app/protocol/`。

LED 操作直接放在 `app/status/status.c`，通过 Zephyr LED API 控制：
启动和故障时常亮，运行时亮/灭各 100 ms；主循环按灯与采样的最近期限休眠，
闪烁不改变采样周期。输出失败阻止喂狗与镜像确认。
后续状态闪烁也在 `status` 中实现，有多个产品复用需求时再抽取模块。

## 仓库布局

```text
IDU-pre/
├── boards/、dts/        # IDU 板定义、早期板级初始化及设备树
├── app/
│   ├── main.c           # 模块初始化顺序、采样周期与休眠
│   ├── acquisition/     # 采集、发布及通信执行流程
│   ├── status/          # 业务状态、LED 策略、健康进度及镜像确认
│   ├── protocol/        # 点表及端口/协议配置；inc/ 配置契约；tables/ 三表
│   └── process/         # 采样映射、工程换算及业务参数回调
├── modules/             # analog_input、gpio、watchdog 及点表/升级/SoC 模块
├── bootloader/          # MCUboot 配置、分区和联合构建
├── sysbuild/            # Zephyr 约定的薄入口
├── conf/                # 应用可选配置
├── tools/、tests/       # 导入、镜像工具及回归测试
├── docs/                # 按主题组织的说明与历史记录
└── scripts/             # 构建、烧录与调试入口
    ├── build.sh
    ├── flash.sh
    ├── debug.sh
    ├── debug-server.py
    └── gd32e505vet6.JLinkScript
```

运行和构建不依赖旧工程或预编译 `plateform.lib`。重新导入和可选互操作测试
才需要显式提供参考源码。产物在工作区 `build/`，开发签名密钥在
`build/keys/`，不进入仓库；清理前备份需要保留的密钥。

## 头文件与启动程序边界

应用以 `app/` 为头文件搜索根。采集、状态、处理头
分别与实现放在 `app/acquisition/`、`app/status/`、`app/process/`；对外配置契约在 `app/protocol/inc/`，内部头文件在 `app/protocol/` 根目录；
跨功能引用使用 `"status/status.h"` 等路径；采样使用 `<acquisition/sample.h>`；IAP 使用 `<iap/iap.h>`、`<iap/sgz.h>`。
通用模块继续保留各自的公开 `include/`，由 Zephyr 模块机制导出。

`app/` 根目录只保留 `main.c`、`CMakeLists.txt` 和 `Kconfig`。
各功能接口按下表引用，
调用通用模块时直接包含对应模块头，不通过应用汇总头间接引入。

| 接口 | 职责 |
| --- | --- |
| `acquisition/acquisition.h` | 每周期采样、发布、转发及健康检查 |
| `status/status.h` | 业务阶段、故障状态、LED 策略及健康进度 |
| `acquisition/sample.h` | 应用原始采样快照及产品采样入口 |
| `<input_sampler/input_sampler.h>` | 由配置和调用者缓冲区驱动的通用组合采样 |
| `status/image_confirm/image_confirm.h` | MCUboot 试运行健康确认 |
| `process/engineering.h` | 仅依赖标准 C 的工程量换算，可用于主机测试 |
| `process/points.h` | 采样发布、周期参数及测点业务回调 |
| `protocol/inc/protocol.h` | 点表注册、`protocol_ports_init()` 注册入口及常驻配置声明 |
| `<transmit/server.h>` | 每端口启动、配置更新及健康维护 |
| `<transmit/registers.h>` | 参数寄存器服务及完整 ADU 扩展 |
| `<collect/port.h>`、`<collect/modbus.h>` | 采集端口初始化及同步 Modbus 事务 |

应用不再设置 `communication/`。`protocol/` 以 const 表提供接口名、路由、协议范围、
命令及参数默认值；`protocol/ports.c` 注册应用收发回调，发送按虚拟口 ID 和链路选择
实际接口，接收按当前南向事务分流；共享链路锁覆盖请求到应答/超时。`collect` 管理采集端口和轮询，`transmit` 管理
转发状态机、协议解析与命令分发，`vport` 提供通用回调端口，`iap` 管理升级。
各模块不引用 IDU 配置头文件。通用模拟设备和模型后端仅在模块测试中使用，
不实现真实 UPP 链路。

`collect` / `transmit` 的协议适配统一放在各自的 `protocols/modbus/`，核心服务留在
`src/`。采集客户端由 `CO_MODBUS_ADAPTER` 控制，转发适配由 `TR_MODBUS_ADAPTER`
控制；纯采集无需启用转发。关闭采集适配时可使用自定义 `transfer` / `read_model`
回调。目录及依赖边界见[模块说明](modules/README.md#protocol-implementation-layout)。

[`bootloader/`](bootloader/README.md) 保存本板 MCUboot 配置、分区和联合构建设置。
MCUboot 源码仍使用 West 工作区中的独立仓库；`app/status/image_confirm/image_confirm.c` 负责应用健康确认。
[`modules/iap/`](modules/iap/README.md) 负责升级接收、解包和验签，分区／内存取自
设备树，密钥来源、镜像上限和保存策略由 `conf/iapv3.conf` 等 Kconfig 配置提供，
产品升级开关归 `app/Kconfig`，运行层和通信层直接调用 `iap_*`，不再保留
`app/iap/` 兼容目录。根 `sysbuild/` 仅保留 Zephyr 约定入口。

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
应用只维护 IDU 一套产品点表和业务，已删除 AIDC 产品分支及专用测试。
IAPv3 是可选升级模块，启停它不会切换产品点表；模块独立配置仍可用于复用和测试。
决策见[单一 IDU 产品方案](docs/adr/0001-single-idu-product.md)。

已记录的桌面验证包括主机回归、native_sim、固件构建和镜像检查。
Zephyr 版的实际启动、UART/DE 时序、Flash 掉电和升级回退仍需实板验证。
历史测试数量、容量和外部支持状态均以文档中的日期或提交为准。

## 点表暂缓的扩展

应用三表使用参考结构 `DM_SIG_CFG_S`、`MODBUS_RTU_TABLE_S` / UPP 模型表、
`MB_TABLE_CELL_T`，数据设备通过 `data_device_cfg` 关联 process/collect，北向入口通过 `tr_forward_cfg` 关联转发表及协议段。信号使用宏或枚举，业务回调直接绑定；
默认值、范围和恢复策略直接内嵌在 `app/protocol/tables/process.c` 的处理表行。
DP 提供 `dp_point_parameters_get()` 查询与初始化前的国家差异覆盖接口，当前产品使用通用参数；
具体国家数据与选择策略尚未接入，见[配置说明](app/protocol/README.md#国家参数预留接口)。
不再设置 `point_metadata` 中间层。

按当前范围，以下扩展暂不启用，后续有需求再单独设计配置：

- **质量码寄存器映射**：10009、10014–10023 暂无映射；内部通信错误和质量状态仍保留。
- **采样过期时间**：不因样本年龄自动置 STALE；通信请求自身的超时和失败处理仍保留。
- **旧参数迁移**：不导入旧 MV-IO 分区，也不保留旧整体快照。
  当前 NVS 按 `(dev_id, sig_id)` 独立保存设定值；默认值和范围保留在 process 表。
  **存储格式不兼容时自动清空 `idu-settings`，记录警告日志并按当前点表重新初始化**，
  状态 `DP_PARAM_STORAGE_RESET` 表示本次清空。新增或重排参数保留已有值；Flash I/O
  错误只报错停写。详见[参数存储规则](docs/point-tables.md)。保存地址默认 1，实际通信地址仍取拨码。

三张点表、默认值、设备绑定声明及登记项为 const，保留在 Flash；设备指针关联视图、运行值、队列和参数缓存留在 RAM。
详细维护方法见[点表配置说明](app/protocol/README.md)。

## 原 README 入口

<a id="已运行的硬件基准与移植配置"></a>
<a id="引脚映射"></a>
<a id="已实现行为"></a>
[硬件基准、引脚与板级行为](docs/hardware.md) 已移至硬件文档。

<a id="基础寄存器接口"></a>
[基础寄存器接口](docs/point-tables.md#idu-pre-配置和寄存器) 见点表文档。

<a id="flash-与启动布局"></a>
<a id="构建与验证"></a>
<a id="mcuboot"></a>
<a id="iapv3"></a>
[Flash 布局](docs/build-and-upgrade.md#flash-与启动布局)、
[构建](docs/build-and-upgrade.md#默认固件构建)、[MCUboot](docs/build-and-upgrade.md#mcuboot)、
[IAPv3 / AES / SGZ](docs/build-and-upgrade.md#iapv3) 和[验证](docs/testing.md) 已按主题归档。
